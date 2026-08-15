import pytest

from ota_flash import find_espota, parse_secrets


def test_parse_secrets_reads_defines():
    text = '''#pragma once
#define WIFI_SSID     "my network"
#define WIFI_PASSWORD "hunter2"
#define OTA_HOSTNAME  "turn-counter"
#define OTA_PASSWORD  "letsplayagame"
'''
    got = parse_secrets(text)
    assert got["OTA_PASSWORD"] == "letsplayagame"
    assert got["OTA_HOSTNAME"] == "turn-counter"
    assert got["WIFI_SSID"] == "my network"


def test_parse_secrets_ignores_comments_and_blanks():
    text = '// #define OTA_PASSWORD "not-this-one"\n#define OTA_PASSWORD "real"\n'
    assert parse_secrets(text)["OTA_PASSWORD"] == "real"


def test_find_espota_picks_the_newest_platform_by_version_not_string(tmp_path):
    # "3.9.0" sorts after "3.10.0" as a string — the newest install must still win.
    for version in ("3.9.0", "3.10.0"):
        tools = tmp_path / "esp32" / "hardware" / "esp32" / version / "tools"
        tools.mkdir(parents=True)
        (tools / "espota.py").write_text("")
    assert find_espota(tmp_path).parent.parent.name == "3.10.0"


def test_find_espota_raises_when_no_platform_installed(tmp_path):
    with pytest.raises(FileNotFoundError):
        find_espota(tmp_path)
