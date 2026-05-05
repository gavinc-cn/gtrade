"""
共享内存封装（StrategyShm Python 侧实现）

对应 C++ strategy_shm.h，通过 mmap 访问同一块共享内存。
内存布局（与 strategy_shm.h 严格一致）：

  [StrategyShmGlobalHeader 512B]
    offset   0: magic         (uint32)
    offset   4: version       (uint32)
    offset   8: channel_a_off (uint32)  ← Channel A 起始偏移
    offset  12: channel_b_off (uint32)  ← Channel B 起始偏移
    offset  16: channel_c_off (uint32)  ← Channel C 起始偏移
    offset  64: heartbeat_us  (int64)   ← Python 写心跳，C++ 读
    offset 128: crash_signal  (uint32)
    offset 132: crash_info    (char[256])

  [ShmChannelHeader 128B]（每个 channel 头部）
    offset   0: write_pos     (uint64)  ← 生产者维护
    offset  64: read_pos      (uint64)  ← 消费者维护
    offset 112: capacity      (uint32)

  帧格式（Channel A/B 数据区）：
    [uint32_t msg_type][uint32_t payload_len][payload bytes，8字节对齐]
    回绕哨兵：msg_type == 0xFFFFFFFF，消费者跳回数据区起始

Channel A：engine → Python（行情/生命周期），Python 是消费者（读）
Channel B：Python → engine（下单/订阅命令），Python 是生产者（写）
Channel C：engine → Python（控制命令），Python 主循环轮询
"""

import mmap
import struct
import time

# ── 常量 ──────────────────────────────────────────────────────────────────────

_HDR_FMT = struct.Struct('<II')   # msg_type (uint32) + payload_len (uint32)
_SENTINEL = 0xFFFFFF_FF           # 回绕哨兵 msg_type

_GLOBAL_HDR_CHAN_A_OFF = 8        # StrategyShmGlobalHeader.channel_a_off
_GLOBAL_HDR_CHAN_B_OFF = 12       # StrategyShmGlobalHeader.channel_b_off
_GLOBAL_HDR_CHAN_C_OFF = 16       # StrategyShmGlobalHeader.channel_c_off
_GLOBAL_HDR_HEARTBEAT  = 64       # StrategyShmGlobalHeader.heartbeat_us (int64)

_CHAN_HDR_WRITE_POS = 0           # ShmChannelHeader.write_pos (uint64)
_CHAN_HDR_READ_POS  = 64          # ShmChannelHeader.read_pos  (uint64)
_CHAN_HDR_CAPACITY  = 112         # ShmChannelHeader.capacity  (uint32)
_CHAN_HDR_SIZE      = 128         # sizeof(ShmChannelHeader)


class StrategyShm:
    """Python 侧共享内存访问封装。

    仅打开已由 C++ 引擎创建的共享内存文件（不负责创建）。
    共享内存文件路径：/dev/shm/gtrade_strat_{strat_id}
    """

    def __init__(self, strat_id: str):
        shm_path = f'/dev/shm/gtrade_strat_{strat_id}'
        self._fd = open(shm_path, 'r+b')
        self._mm = mmap.mmap(self._fd.fileno(), 0)
        self._parse_global_header()

    def _parse_global_header(self):
        """解析 StrategyShmGlobalHeader，缓存各 channel 的偏移和容量。"""
        mm = self._mm

        # 各 channel 头部起始偏移
        self._chan_a_hdr = struct.unpack_from('<I', mm, _GLOBAL_HDR_CHAN_A_OFF)[0]
        self._chan_b_hdr = struct.unpack_from('<I', mm, _GLOBAL_HDR_CHAN_B_OFF)[0]
        self._chan_c_hdr = struct.unpack_from('<I', mm, _GLOBAL_HDR_CHAN_C_OFF)[0]

        # 各 channel 数据区起始偏移（header 之后）
        self._chan_a_data = self._chan_a_hdr + _CHAN_HDR_SIZE
        self._chan_b_data = self._chan_b_hdr + _CHAN_HDR_SIZE
        self._chan_c_data = self._chan_c_hdr + _CHAN_HDR_SIZE

        # 各 channel 数据区容量（字节）
        self._chan_a_cap = struct.unpack_from('<I', mm, self._chan_a_hdr + _CHAN_HDR_CAPACITY)[0]
        self._chan_b_cap = struct.unpack_from('<I', mm, self._chan_b_hdr + _CHAN_HDR_CAPACITY)[0]

        # 初始化本地读写位置指针（与 shm 中的值同步）
        self._a_read_pos  = struct.unpack_from('<Q', mm, self._chan_a_hdr + _CHAN_HDR_READ_POS)[0]
        self._b_write_pos = struct.unpack_from('<Q', mm, self._chan_b_hdr + _CHAN_HDR_WRITE_POS)[0]

    # ── Channel A：读行情 ─────────────────────────────────────────────────────

    def try_read_data(self) -> tuple[int, bytes] | None:
        """非阻塞尝试读取 Channel A 中的下一帧。

        返回 (msg_type, payload_bytes)，或在无数据时返回 None。
        跳过回绕哨兵帧，自动更新 read_pos。
        """
        mm = self._mm
        cap = self._chan_a_cap
        data_off = self._chan_a_data

        while True:
            pos_in_buf = self._a_read_pos % cap
            abs_pos = data_off + pos_in_buf

            msg_type, payload_len = _HDR_FMT.unpack_from(mm, abs_pos)

            if msg_type == 0:
                # 无数据
                return None

            if msg_type == _SENTINEL:
                # 回绕哨兵：跳到数据区起始
                remaining = cap - pos_in_buf
                self._a_read_pos += remaining
                # 更新 shm 中的 read_pos
                struct.pack_into('<Q', mm, self._chan_a_hdr + _CHAN_HDR_READ_POS,
                                 self._a_read_pos)
                continue

            # 正常数据帧：读取 payload
            payload = bytes(mm[abs_pos + 8: abs_pos + 8 + payload_len])
            aligned = (payload_len + 7) & ~7
            self._a_read_pos += 8 + aligned
            # 更新 shm 中的 read_pos（供 C++ 侧 HWM 检测）
            struct.pack_into('<Q', mm, self._chan_a_hdr + _CHAN_HDR_READ_POS,
                             self._a_read_pos)
            return msg_type, payload

    # ── Channel B：写命令 ─────────────────────────────────────────────────────

    def write_request(self, msg_type: int, payload: bytes) -> bool:
        """向 Channel B 写入一帧（Python → engine 命令）。

        若 Channel B 剩余空间不足，写入回绕哨兵后从头写入。
        返回 True 表示成功，False 表示 Channel B 满（丢弃本帧）。
        """
        mm = self._mm
        cap = self._chan_b_cap
        data_off = self._chan_b_data
        payload_len = len(payload)
        aligned = (payload_len + 7) & ~7
        frame_size = 8 + aligned  # header(8B) + aligned payload

        pos_in_buf = self._b_write_pos % cap
        remaining = cap - pos_in_buf

        if remaining < frame_size:
            if remaining < 8:
                # 剩余空间连哨兵头部都写不下（极端情况）
                return False
            # 写回绕哨兵，跳回数据区起始
            _HDR_FMT.pack_into(mm, data_off + pos_in_buf, _SENTINEL, 0)
            self._b_write_pos += remaining
            pos_in_buf = 0

        abs_pos = data_off + pos_in_buf
        # 先写 payload，再写 header（防止消费者提前读到不完整帧）
        mm[abs_pos + 8: abs_pos + 8 + payload_len] = payload
        _HDR_FMT.pack_into(mm, abs_pos, msg_type, payload_len)
        self._b_write_pos += frame_size
        # 更新 shm 中的 write_pos
        struct.pack_into('<Q', mm, self._chan_b_hdr + _CHAN_HDR_WRITE_POS,
                         self._b_write_pos)
        return True

    # ── Channel C：读控制命令 ─────────────────────────────────────────────────

    def read_ctrl(self) -> int:
        """非阻塞读 Channel C 控制命令，返回 msg_type 或 0（无数据）。

        Channel C 是单帧槽（不是 ring buffer），读取后清零。
        """
        mm = self._mm
        abs_pos = self._chan_c_data
        msg_type, _ = _HDR_FMT.unpack_from(mm, abs_pos)
        if msg_type != 0:
            # 消费后清零 msg_type，防止重复触发
            struct.pack_into('<I', mm, abs_pos, 0)
        return msg_type

    # ── 心跳 ──────────────────────────────────────────────────────────────────

    def write_heartbeat(self):
        """写入当前时间（微秒）到 GlobalHeader.heartbeat_us。

        C++ SubprocessManager 检测此字段判断子进程是否存活，
        超过 heartbeat_timeout_ms 未更新视为死亡。
        Python runner 每 500ms 调用一次（与 C++ runner 保持一致）。
        """
        now_us = int(time.time() * 1_000_000)
        struct.pack_into('<q', self._mm, _GLOBAL_HDR_HEARTBEAT, now_us)

    def close(self):
        """释放 mmap 和文件描述符。"""
        if self._mm:
            self._mm.close()
        if self._fd:
            self._fd.close()

    def __del__(self):
        self.close()
