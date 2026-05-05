"""测试 Walk-Forward 窗口生成逻辑。"""

from autoresearch.optimizer.walk_forward import generate_windows


def test_generate_windows_count():
    assert len(generate_windows("20240101_0000", "20241231_0000", n_windows=4)) == 4


def test_generate_windows_order():
    for w in generate_windows("20240101_0000", "20241231_0000", n_windows=3):
        assert w["train_start"] < w["train_end"] == w["test_start"] < w["test_end"]


def test_generate_windows_no_overlap():
    ws = generate_windows("20240101_0000", "20241231_0000", n_windows=4)
    for i in range(1, len(ws)):
        assert ws[i]["train_start"] >= ws[i - 1]["test_start"]
