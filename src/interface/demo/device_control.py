import struct

class DeviceEvent:
    _fmt = '32siiii32s32s32s32s'
    _size = struct.calcsize(_fmt)

    def __init__(self, type, x, y, dy, dx, button, pressed, key, time):
        self.type = type
        self.x = x
        self.y = y
        self.dy = dy
        self.dx = dx
        self.button = button
        self.pressed = pressed
        self.key = key
        self.time = time

    @classmethod
    def from_bytes(cls, data):
        if len(data) != cls._size:
            raise ValueError('Data length does not match struct size.')
        type, x, y, dy, dx, button, pressed, key, time = struct.unpack(cls._fmt, data)
        type = type.decode('ascii').rstrip('\0')
        button = button.decode('ascii').rstrip('\0')
        pressed = pressed.decode('ascii').rstrip('\0')
        key = key.decode('ascii').rstrip('\0')
        time = time.decode('ascii').rstrip('\0')
        return cls(type, x, y, dy, dx, button, pressed, key, time)

    def to_bytes(self):
        return struct.pack(self._fmt, self.type.encode('ascii').ljust(32, b'\0')[:32], self.x, self.y, self.dy, self.dx, self.button.encode('ascii').ljust(32, b'\0')[:32], self.pressed.encode('ascii').ljust(32, b'\0')[:32], self.key.encode('ascii').ljust(32, b'\0')[:32], self.time.encode('ascii').ljust(32, b'\0')[:32])
