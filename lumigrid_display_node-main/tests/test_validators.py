"""Tests for the validators module."""

import pytest
from lumigrid.validators import (
    ValidationError,
    validate_brightness,
    validate_mode,
    validate_message,
    validate_hex_color,
    validate_font_filename,
    validate_font_path,
    validate_frame_data,
    validate_led_matrix_config,
    sanitize_path,
    sanitize_text,
    sanitize_frame_config,
    VALID_MODES,
    VALID_FRAME_TYPES,
    MIN_BRIGHTNESS,
    MAX_BRIGHTNESS,
    MAX_MESSAGE_LENGTH,
)


class TestValidateBrightness:
    """Tests for validate_brightness function."""
    
    def test_valid_brightness_integer(self):
        """Test valid integer brightness values."""
        assert validate_brightness(0) == 0
        assert validate_brightness(50) == 50
        assert validate_brightness(100) == 100
    
    def test_valid_brightness_string(self):
        """Test valid string brightness values."""
        assert validate_brightness("50") == 50
        assert validate_brightness("0") == 0
        assert validate_brightness("100") == 100
    
    def test_brightness_below_minimum(self):
        """Test brightness below minimum."""
        with pytest.raises(ValidationError) as exc_info:
            validate_brightness(-1)
        assert "between" in exc_info.value.message
        assert exc_info.value.field == "brightness"
    
    def test_brightness_above_maximum(self):
        """Test brightness above maximum."""
        with pytest.raises(ValidationError) as exc_info:
            validate_brightness(101)
        assert "between" in exc_info.value.message
    
    def test_brightness_invalid_type(self):
        """Test brightness with invalid type."""
        with pytest.raises(ValidationError) as exc_info:
            validate_brightness("invalid")
        assert "integer" in exc_info.value.message
    
    def test_brightness_none(self):
        """Test brightness with None."""
        with pytest.raises(ValidationError):
            validate_brightness(None)


class TestValidateMode:
    """Tests for validate_mode function."""
    
    def test_valid_modes(self):
        """Test all valid modes."""
        for mode in VALID_MODES:
            assert validate_mode(mode) == mode
    
    def test_custom_available_modes(self):
        """Test with custom available modes."""
        custom_modes = {'custom1', 'custom2'}
        assert validate_mode('custom1', custom_modes) == 'custom1'
    
    def test_invalid_mode(self):
        """Test invalid mode."""
        with pytest.raises(ValidationError) as exc_info:
            validate_mode('invalid_mode')
        assert "Invalid mode" in exc_info.value.message
        assert exc_info.value.field == "mode"
    
    def test_mode_not_string(self):
        """Test mode with non-string type."""
        with pytest.raises(ValidationError) as exc_info:
            validate_mode(123)
        assert "string" in exc_info.value.message


class TestValidateMessage:
    """Tests for validate_message function."""
    
    def test_valid_message(self):
        """Test valid message."""
        assert validate_message("Hello World") == "Hello World"
        assert validate_message("") == ""
    
    def test_message_too_long(self):
        """Test message exceeding max length."""
        long_message = "x" * (MAX_MESSAGE_LENGTH + 1)
        with pytest.raises(ValidationError) as exc_info:
            validate_message(long_message)
        assert "too long" in exc_info.value.message
    
    def test_message_not_string(self):
        """Test message with non-string type."""
        with pytest.raises(ValidationError):
            validate_message(123)


class TestValidateHexColor:
    """Tests for validate_hex_color function."""
    
    def test_valid_6_digit_hex(self):
        """Test valid 6-digit hex color."""
        assert validate_hex_color("#FF0000") == "#FF0000"
        assert validate_hex_color("FF0000") == "#FF0000"
        assert validate_hex_color("#ff0000") == "#FF0000"
    
    def test_valid_3_digit_hex(self):
        """Test valid 3-digit hex color."""
        assert validate_hex_color("#F00") == "#FF0000"
        assert validate_hex_color("F00") == "#FF0000"
    
    def test_invalid_hex_format(self):
        """Test invalid hex format."""
        with pytest.raises(ValidationError) as exc_info:
            validate_hex_color("invalid")
        assert "hex color" in exc_info.value.message.lower()
    
    def test_invalid_hex_length(self):
        """Test invalid hex length."""
        with pytest.raises(ValidationError):
            validate_hex_color("#FF00")  # 4 digits
    
    def test_hex_color_not_string(self):
        """Test hex color with non-string type."""
        with pytest.raises(ValidationError):
            validate_hex_color(0xFF0000)


class TestValidateFontFilename:
    """Tests for validate_font_filename function."""
    
    def test_valid_ttf_filename(self):
        """Test valid TTF filename."""
        assert validate_font_filename("font.ttf") == "font.ttf"
        assert validate_font_filename("MyFont.TTF") == "MyFont.TTF"
    
    def test_valid_otf_filename(self):
        """Test valid OTF filename."""
        assert validate_font_filename("font.otf") == "font.otf"
        assert validate_font_filename("MyFont.OTF") == "MyFont.OTF"
    
    def test_invalid_extension(self):
        """Test invalid file extension."""
        with pytest.raises(ValidationError) as exc_info:
            validate_font_filename("font.woff")
        assert "font file type" in exc_info.value.message.lower()
    
    def test_path_traversal_attempt(self):
        """Test path traversal prevention."""
        with pytest.raises(ValidationError) as exc_info:
            validate_font_filename("../font.ttf")
        assert "invalid" in exc_info.value.message.lower()
    
    def test_absolute_path_attempt(self):
        """Test absolute path prevention."""
        with pytest.raises(ValidationError):
            validate_font_filename("/etc/passwd")
    
    def test_empty_filename(self):
        """Test empty filename."""
        with pytest.raises(ValidationError):
            validate_font_filename("")


class TestValidateFontPath:
    """Tests for validate_font_path function."""
    
    def test_valid_path(self, tmp_path):
        """Test valid path within allowed directory."""
        allowed_dirs = [str(tmp_path)]
        font_path = str(tmp_path / "font.ttf")
        assert validate_font_path(font_path, allowed_dirs) == font_path
    
    def test_path_outside_allowed(self, tmp_path):
        """Test path outside allowed directories."""
        allowed_dirs = [str(tmp_path)]
        with pytest.raises(ValidationError) as exc_info:
            validate_font_path("/etc/passwd", allowed_dirs)
        assert "outside" in exc_info.value.message
    
    def test_empty_path(self):
        """Test empty path."""
        with pytest.raises(ValidationError):
            validate_font_path("", ["/tmp"])


class TestValidateFrameData:
    """Tests for validate_frame_data function."""
    
    def test_valid_frame_data(self):
        """Test valid frame data."""
        frame_data = {
            'type': 'time',
            'name': 'Test Frame',
            'enabled': True,
            'duration': 5,
            'text': 'Hello',
            'colors': {
                'text': '#FFFFFF',
                'background': '#000000'
            }
        }
        result = validate_frame_data(frame_data)
        assert result['type'] == 'time'
        assert result['name'] == 'Test Frame'
        assert result['enabled'] is True
        assert result['duration'] == 5
    
    def test_invalid_frame_type(self):
        """Test invalid frame type."""
        frame_data = {'type': 'invalid_type'}
        with pytest.raises(ValidationError) as exc_info:
            validate_frame_data(frame_data)
        assert "Invalid frame type" in exc_info.value.message
    
    def test_invalid_duration(self):
        """Test invalid duration."""
        frame_data = {'duration': 0}
        with pytest.raises(ValidationError) as exc_info:
            validate_frame_data(frame_data)
        assert "Duration" in exc_info.value.message
    
    def test_invalid_color(self):
        """Test invalid color format."""
        frame_data = {'colors': {'text': 'not-a-color'}}
        with pytest.raises(ValidationError):
            validate_frame_data(frame_data)
    
    def test_frame_data_not_dict(self):
        """Test frame data with non-dict type."""
        with pytest.raises(ValidationError) as exc_info:
            validate_frame_data("not a dict")
        assert "dictionary" in exc_info.value.message


class TestValidateLedMatrixConfig:
    """Tests for validate_led_matrix_config function."""
    
    def test_valid_config(self):
        """Test valid LED matrix config."""
        config = {
            'rows': 32,
            'cols': 64,
            'brightness': 50,
            'chain_length': 1
        }
        result = validate_led_matrix_config(config)
        assert result['rows'] == 32
        assert result['cols'] == 64
        assert result['brightness'] == 50
    
    def test_brightness_out_of_range(self):
        """Test brightness out of valid range."""
        config = {'brightness': 150}
        with pytest.raises(ValidationError) as exc_info:
            validate_led_matrix_config(config)
        assert "brightness" in exc_info.value.field
    
    def test_invalid_rows(self):
        """Test invalid rows value."""
        config = {'rows': 1000}  # Too high
        with pytest.raises(ValidationError):
            validate_led_matrix_config(config)
    
    def test_config_not_dict(self):
        """Test config with non-dict type."""
        with pytest.raises(ValidationError):
            validate_led_matrix_config("not a dict")


class TestSanitizePath:
    """Tests for sanitize_path function."""
    
    def test_valid_path(self, tmp_path):
        """Test valid path within base directory."""
        result = sanitize_path("subdir/file.txt", str(tmp_path))
        assert result is not None
        assert str(tmp_path) in result
    
    def test_path_traversal_blocked(self, tmp_path):
        """Test path traversal is blocked."""
        result = sanitize_path("../../../etc/passwd", str(tmp_path))
        assert result is None
    
    def test_empty_path(self, tmp_path):
        """Test empty path returns None."""
        result = sanitize_path("", str(tmp_path))
        assert result is None
    
    def test_none_path(self, tmp_path):
        """Test None path returns None."""
        result = sanitize_path(None, str(tmp_path))
        assert result is None


class TestSanitizeText:
    """Tests for sanitize_text function."""
    
    def test_removes_html_characters(self):
        """Test that HTML special characters are removed."""
        result = sanitize_text("<script>alert('xss')</script>")
        assert '<' not in result
        assert '>' not in result
        assert "'" not in result
        assert result == "scriptalert(xss)/script"
    
    def test_removes_quotes(self):
        """Test that quotes are removed."""
        result = sanitize_text('Hello "World" and \'Universe\'')
        assert '"' not in result
        assert "'" not in result
        assert result == "Hello World and Universe"
    
    def test_preserves_safe_text(self):
        """Test that safe text is preserved."""
        safe_text = "Hello World 123!@#$%^&*()-_=+[]{}|;:,./?`~"
        result = sanitize_text(safe_text)
        # Only quotes and angle brackets should be removed
        assert "Hello World 123!@#$%^&*()-_=+[]{}|;:,./?`~" == result
    
    def test_truncates_long_text(self):
        """Test that text is truncated to max_length."""
        long_text = "x" * 100
        result = sanitize_text(long_text, max_length=50)
        assert len(result) == 50
    
    def test_converts_non_string(self):
        """Test that non-strings are converted."""
        result = sanitize_text(12345)
        assert result == "12345"


class TestSanitizeFrameConfig:
    """Tests for sanitize_frame_config function."""
    
    def test_sanitizes_text_field(self):
        """Test that text field is sanitized."""
        config = {'text': '<script>alert("xss")</script>'}
        result = sanitize_frame_config(config)
        assert '<' not in result['text']
        assert '>' not in result['text']
    
    def test_sanitizes_name_field(self):
        """Test that name field is sanitized."""
        config = {'name': "Test <Frame> 'Name'"}
        result = sanitize_frame_config(config)
        assert '<' not in result['name']
        assert '>' not in result['name']
    
    def test_sanitizes_message_field(self):
        """Test that message field is sanitized."""
        config = {'message': 'Hello "World"'}
        result = sanitize_frame_config(config)
        assert '"' not in result['message']
    
    def test_sanitizes_invalid_format(self):
        """Test that invalid format strings are replaced."""
        config = {'format': '${cmd}'}  # Potentially dangerous format
        result = sanitize_frame_config(config)
        assert result['format'] == '%H:%M:%S'  # Default safe format
    
    def test_preserves_valid_format(self):
        """Test that valid format strings are preserved."""
        config = {'format': '%Y-%m-%d %H:%M:%S'}
        result = sanitize_frame_config(config)
        assert result['format'] == '%Y-%m-%d %H:%M:%S'
    
    def test_handles_non_dict_input(self):
        """Test that non-dict input returns empty dict."""
        result = sanitize_frame_config("not a dict")
        assert result == {}
    
    def test_preserves_other_fields(self):
        """Test that other fields are preserved."""
        config = {
            'type': 'time',
            'duration': 5,
            'enabled': True,
            'colors': {'text': '#FFFFFF'}
        }
        result = sanitize_frame_config(config)
        assert result['type'] == 'time'
        assert result['duration'] == 5
        assert result['enabled'] is True
        assert result['colors'] == {'text': '#FFFFFF'}
