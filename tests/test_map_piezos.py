from map_piezos import classify


def test_prompt_line():
    assert classify("MAP side 3 - tap the lit seat") == ("prompt", {"side": 3})


def test_assignment_line():
    assert classify("MAP side 3 = GPIO 6") == ("assign", {"side": 3, "gpio": 6})


def test_duplicate_channel_line():
    assert classify("MAP DUP - GPIO 6 already mapped to side 1, retry") == (
        "dup",
        {"gpio": 6, "owner": 1},
    )


def test_terminal_lines():
    assert classify("MAP DONE - saved to NVS")[0] == "done"
    assert classify("MAP TIMEOUT - nothing saved")[0] == "timeout"
    assert classify("MAP ABORT - nothing saved")[0] == "abort"


def test_source_line_yields_the_pin_order():
    kind, payload = classify("uint8_t sidePiezoPin[NUM_SIDES] = {1, 2, 4, 5, 6, 7, 8, 9};")
    assert kind == "source"
    assert payload["pins"] == [1, 2, 4, 5, 6, 7, 8, 9]


def test_game_chatter_is_not_mistaken_for_protocol():
    # The board keeps printing gameplay lines; none of them may parse as protocol.
    for line in [
        "Tap on side 2 ignored - not the current seat",
        "Setup: side 3 IN",
        "Side LED counts: 29 28 27 27 27 28 28 27 (total 221)",
        "MAP START - tap each lit side; q aborts",
        "",
    ]:
        assert classify(line)[0] == "other"


def test_trailing_whitespace_and_cr_are_tolerated():
    assert classify("MAP side 0 = GPIO 1\r\n") == ("assign", {"side": 0, "gpio": 1})
