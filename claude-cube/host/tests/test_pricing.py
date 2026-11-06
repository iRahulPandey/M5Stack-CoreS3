from __future__ import annotations

import pytest

from claudecube.pricing import context_window, display_name, message_cost

MILLION = 1_000_000


def usage(
    input_tokens: int = 0,
    output_tokens: int = 0,
    cache_read: int = 0,
    write_5m: int = 0,
    write_1h: int = 0,
) -> dict:
    return {
        "input_tokens": input_tokens,
        "output_tokens": output_tokens,
        "cache_read_input_tokens": cache_read,
        "cache_creation_input_tokens": write_5m + write_1h,
        "cache_creation": {
            "ephemeral_5m_input_tokens": write_5m,
            "ephemeral_1h_input_tokens": write_1h,
        },
    }


class TestMessageCost:
    def test_should_price_opus_5_5_input_and_output_at_list_rates(self) -> None:
        cost = message_cost("claude-opus-5-5", usage(input_tokens=MILLION, output_tokens=MILLION), "standard")
        assert cost == pytest.approx(4.0 + 20.0)

    def test_should_use_opus_5_5_discounted_cache_read_rate(self) -> None:
        assert message_cost("claude-opus-5-5", usage(cache_read=MILLION), "standard") == pytest.approx(0.20)

    def test_should_price_cache_writes_by_ttl(self) -> None:
        cost = message_cost("claude-opus-5-5", usage(write_5m=MILLION, write_1h=MILLION), "standard")
        assert cost == pytest.approx(4.0 * 1.25 + 4.0 * 2.0)

    def test_should_treat_unsplit_cache_writes_as_five_minute(self) -> None:
        raw = {"input_tokens": 0, "output_tokens": 0, "cache_creation_input_tokens": MILLION}
        assert message_cost("claude-sonnet-5-5", raw, None) == pytest.approx(2.0 * 1.25)

    def test_should_double_opus_5_5_rates_in_fast_mode(self) -> None:
        cost = message_cost("claude-opus-5-5", usage(input_tokens=MILLION, output_tokens=MILLION), "fast")
        assert cost == pytest.approx(8.0 + 40.0)

    def test_should_use_long_prompt_rates_for_haiku_5_5_above_100k(self) -> None:
        cost = message_cost("claude-haiku-5-5", usage(input_tokens=150_000, output_tokens=MILLION), "standard")
        assert cost == pytest.approx(0.15 * 0.50 + 2.50)

    def test_should_resolve_date_suffixed_model_ids(self) -> None:
        cost = message_cost("claude-haiku-4-5-20251001", usage(output_tokens=MILLION), "standard")
        assert cost == pytest.approx(5.0)

    def test_should_return_none_for_unknown_models(self) -> None:
        assert message_cost("claude-unknown-9", usage(output_tokens=10), "standard") is None


class TestDisplayName:
    @pytest.mark.parametrize(
        ("model_id", "expected"),
        [
            ("claude-opus-5-5", "Opus 5.5"),
            ("claude-fable-5-1", "Fable 5.1"),
            ("claude-opus-5", "Opus 5"),
            ("claude-haiku-4-5-20251001", "Haiku 4.5"),
            ("claude-3-5-sonnet-20241022", "Sonnet 3.5"),
        ],
    )
    def test_should_turn_model_ids_into_short_names(self, model_id: str, expected: str) -> None:
        assert display_name(model_id) == expected


class TestContextWindow:
    def test_should_report_1m_for_current_models(self) -> None:
        assert context_window("claude-opus-5-5") == MILLION

    def test_should_report_200k_for_haiku_4_5(self) -> None:
        assert context_window("claude-haiku-4-5-20251001") == 200_000

    def test_should_fall_back_to_200k_for_unknown_models(self) -> None:
        assert context_window("claude-mystery-1") == 200_000
