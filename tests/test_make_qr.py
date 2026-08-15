import pytest

from make_qr import normalize_url


def test_bare_ip_gets_scheme_and_root_path():
    assert normalize_url("192.168.0.50") == "http://192.168.0.50/"


def test_bare_hostname_gets_scheme_and_root_path():
    assert normalize_url("turn-counter.local") == "http://turn-counter.local/"


def test_existing_scheme_is_preserved():
    assert normalize_url("https://turn-counter.local/") == "https://turn-counter.local/"


def test_existing_path_is_kept():
    assert normalize_url("http://192.168.0.50/panel") == "http://192.168.0.50/panel"


def test_surrounding_whitespace_is_stripped():
    assert normalize_url("  192.168.0.50\n") == "http://192.168.0.50/"


def test_empty_is_rejected():
    with pytest.raises(ValueError):
        normalize_url("")


def test_internal_whitespace_is_rejected():
    with pytest.raises(ValueError):
        normalize_url("192.168.0.50 /panel")


def test_non_web_scheme_is_rejected():
    # A QR that opens a telnet handler is not what anyone stuck under a table wants.
    with pytest.raises(ValueError):
        normalize_url("ftp://192.168.0.50/")


def test_scheme_with_no_host_is_rejected():
    with pytest.raises(ValueError):
        normalize_url("http://")
