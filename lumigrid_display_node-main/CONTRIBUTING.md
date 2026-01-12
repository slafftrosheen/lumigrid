# Contributing to LumiGrid Display Node

Thank you for your interest in contributing to LumiGrid Display Node! This document provides guidelines and instructions for contributing.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [Development Workflow](#development-workflow)
- [Coding Standards](#coding-standards)
- [Testing](#testing)
- [Documentation](#documentation)
- [Submitting Changes](#submitting-changes)

## Code of Conduct

### Our Pledge

We are committed to providing a welcoming and inspiring community for all. Please be respectful and constructive in all interactions.

### Expected Behavior

- Be respectful and inclusive
- Welcome newcomers and help them get started
- Focus on what is best for the community
- Show empathy towards other community members

### Unacceptable Behavior

- Harassment, discrimination, or offensive comments
- Personal attacks or trolling
- Publishing others' private information
- Other conduct which could reasonably be considered inappropriate

## Getting Started

### Ways to Contribute

- **Report Bugs**: Submit detailed bug reports via GitHub Issues
- **Suggest Features**: Propose new features or improvements
- **Write Code**: Fix bugs or implement new features
- **Improve Documentation**: Enhance or clarify documentation
- **Write Tests**: Add test coverage for existing code
- **Review Code**: Review pull requests from other contributors

### Before You Start

1. Check existing [Issues](https://github.com/algorhythmicdev/lumigrid_display_node/issues) and [Pull Requests](https://github.com/algorhythmicdev/lumigrid_display_node/pulls)
2. For major changes, open an issue first to discuss your proposal
3. Read this contributing guide completely

## Development Setup

### Prerequisites

- **Raspberry Pi** (for testing on actual hardware) or
- **Linux development machine** (for code development and testing)
- **Python 3.7+**
- **Git**
- **Text editor or IDE** (VS Code, PyCharm, vim, etc.)

### Setting Up Development Environment

1. **Fork the repository** on GitHub

2. **Clone your fork**:
   ```bash
   git clone https://github.com/YOUR_USERNAME/lumigrid_display_node.git
   cd lumigrid_display_node
   ```

3. **Add upstream remote**:
   ```bash
   git remote add upstream https://github.com/algorhythmicdev/lumigrid_display_node.git
   ```

4. **Create a virtual environment**:
   ```bash
   python3 -m venv venv
   source venv/bin/activate  # On Windows: venv\Scripts\activate
   ```

5. **Install development dependencies**:
   ```bash
   pip install --upgrade pip setuptools wheel
   pip install -r requirements.txt
   pip install -r requirements-dev.txt  # If exists
   ```

6. **Install in development mode**:
   ```bash
   pip install -e .
   ```

### Development Without Hardware

For development without actual LED hardware:

1. **Use mock/simulation mode** (if implemented)
2. **Write and run unit tests** without hardware
3. **Test web interface** functionality
4. **Validate API endpoints** with mock data

## Development Workflow

### Creating a Branch

Always create a new branch for your changes:

```bash
# Update your main branch
git checkout main
git pull upstream main

# Create a feature branch
git checkout -b feature/your-feature-name

# Or for bug fixes
git checkout -b fix/bug-description
```

### Branch Naming Conventions

- `feature/` - New features
- `fix/` - Bug fixes
- `docs/` - Documentation changes
- `refactor/` - Code refactoring
- `test/` - Adding or updating tests

Examples:
- `feature/add-weather-integration`
- `fix/brightness-validation-error`
- `docs/improve-api-examples`

### Making Changes

1. **Write clean, readable code** following our coding standards
2. **Add tests** for new functionality
3. **Update documentation** as needed
4. **Test your changes** thoroughly
5. **Commit regularly** with clear commit messages

### Commit Message Guidelines

Follow these conventions:

```
type(scope): brief description

Detailed explanation if needed

Fixes #issue_number
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code style changes (formatting, etc.)
- `refactor`: Code refactoring
- `test`: Adding or updating tests
- `chore`: Maintenance tasks

**Examples:**

```
feat(presets): add support for humidity display

Added humidity frame type with configurable unit and icon.
Includes validation and schema updates.

Fixes #123
```

```
fix(sync): handle UDP timeout gracefully

Improved error handling when sync master is unavailable.
Added automatic fallback to local clock mode.

Fixes #456
```

## Coding Standards

### Python Style Guide

We follow [PEP 8](https://www.python.org/dev/peps/pep-0008/) with some modifications:

- **Line length**: 100 characters (not 80)
- **Indentation**: 4 spaces (no tabs)
- **Imports**: Organized into stdlib, third-party, and local
- **Type hints**: Use type hints for function signatures
- **Docstrings**: Google style for all public functions/classes

### Code Style Examples

**Good:**

```python
from typing import Dict, Any, Optional

def create_preset(
    preset_name: str,
    frame_config: Dict[str, Any],
    description: Optional[str] = None
) -> Dict[str, Any]:
    """Create a new display preset.
    
    Args:
        preset_name: Unique identifier for the preset
        frame_config: Frame configuration dictionary
        description: Optional description of the preset
        
    Returns:
        Dictionary containing the created preset
        
    Raises:
        ValidationError: If preset_name or frame_config is invalid
    """
    if not preset_name:
        raise ValidationError("preset_name is required")
    
    # Implementation here
    return {"preset_name": preset_name, "frame_config": frame_config}
```

**Bad:**

```python
def create_preset(preset_name,frame_config,description=None):
    if not preset_name: raise ValidationError("preset_name is required")
    return {"preset_name":preset_name,"frame_config":frame_config}
```

### Formatting Tools

Use these tools to ensure consistent formatting:

```bash
# Install formatting tools
pip install black flake8 mypy

# Format code with black
black lumigrid/

# Check with flake8
flake8 lumigrid/ --max-line-length=100

# Type checking with mypy
mypy lumigrid/
```

### Project Structure

```
lumigrid_display_node/
├── lumigrid/              # Main package
│   ├── __init__.py
│   ├── config.py          # Configuration management
│   ├── controller.py      # Main controller
│   ├── display/           # Display logic
│   ├── sensors/           # Sensor integrations
│   ├── sync/              # UDP sync protocol
│   └── web/               # Web interface
├── tests/                 # Test files
├── docs/                  # Documentation
├── preset_examples/       # Example presets
├── schemas/               # JSON schemas
├── config.yaml.example    # Example configuration
└── requirements.txt       # Dependencies
```

## Testing

### Running Tests

```bash
# Run all tests
python -m pytest

# Run with coverage
python -m pytest --cov=lumigrid --cov-report=html

# Run specific test file
python -m pytest tests/test_validators.py

# Run specific test
python -m pytest tests/test_validators.py::test_validate_brightness
```

### Writing Tests

Place tests in the `tests/` directory with filename `test_*.py`:

```python
import pytest
from lumigrid.validators import validate_brightness, ValidationError

def test_validate_brightness_valid():
    """Test brightness validation with valid values."""
    assert validate_brightness(50) == 50
    assert validate_brightness(0) == 0
    assert validate_brightness(100) == 100

def test_validate_brightness_invalid():
    """Test brightness validation with invalid values."""
    with pytest.raises(ValidationError):
        validate_brightness(-1)
    
    with pytest.raises(ValidationError):
        validate_brightness(101)
    
    with pytest.raises(ValidationError):
        validate_brightness("invalid")
```

### Test Coverage

- Aim for **80%+ coverage** for new code
- Test both success and error cases
- Include edge cases and boundary conditions
- Mock external dependencies (hardware, network)

## Documentation

### Types of Documentation

1. **Code Comments**: Explain complex logic
2. **Docstrings**: Document all public functions/classes
3. **README**: User-facing documentation
4. **API Reference**: Complete API documentation
5. **Guides**: Setup, integration, and usage guides
6. **In-App Documentation**: Web UI help pages (`/help`, `/faq`)

### Documentation Standards

- Use **clear, concise language**
- Include **code examples** where helpful
- Keep documentation **up-to-date** with code changes
- Use **proper Markdown formatting**
- Ensure **accessibility** (WCAG 2.1 compliance for web docs)
- Make web documentation **responsive** (mobile-friendly)

### Updating Documentation

When making changes that affect documentation:

1. Update relevant docstrings in code
2. Update README.md if user-facing changes
3. Update API_REFERENCE.md for API changes
4. Update in-app documentation (`help.html`, `faq.html`) for UI changes
5. Add examples to appropriate guides
6. Update CHANGELOG.md
7. Update DOCS_INDEX.md if adding new documentation files

### In-App Documentation

The web UI includes comprehensive documentation accessible to users:

- **Help & Documentation** (`/help`) - Main documentation hub
  - Hardware requirements and setup
  - Installation and configuration guides
  - Web interface tutorials
  - Troubleshooting guides
  - API reference with examples
  - Developer documentation links
  
- **FAQ** (`/faq`) - Frequently asked questions
  - Organized by category
  - Searchable content
  - Quick problem-solving reference

When updating web UI features, ensure the in-app documentation is updated accordingly.

## Submitting Changes

### Before Submitting

- [ ] Code follows style guidelines
- [ ] All tests pass
- [ ] New tests added for new functionality
- [ ] Documentation updated
- [ ] Commits have clear messages
- [ ] Branch is up-to-date with main

### Pull Request Process

1. **Push your branch** to your fork:
   ```bash
   git push origin feature/your-feature-name
   ```

2. **Open a Pull Request** on GitHub:
   - Use a clear, descriptive title
   - Reference related issues
   - Describe what changes you made and why
   - Include screenshots for UI changes
   - List any breaking changes

3. **Pull Request Template**:
   ```markdown
   ## Description
   Brief description of changes
   
   ## Related Issues
   Fixes #123
   
   ## Changes Made
   - Added feature X
   - Fixed bug Y
   - Updated documentation
   
   ## Testing
   - [ ] Tested on Raspberry Pi 4
   - [ ] All unit tests pass
   - [ ] Manual testing completed
   
   ## Screenshots (if applicable)
   [Add screenshots here]
   
   ## Breaking Changes
   List any breaking changes or migration notes
   ```

4. **Respond to review feedback**:
   - Address all comments
   - Push updates to the same branch
   - Be open to suggestions

5. **Merge requirements**:
   - All CI checks pass
   - At least one approval from maintainer
   - No unresolved conversations
   - Branch is up-to-date with main

### After Merge

1. **Delete your branch** (if desired):
   ```bash
   git branch -d feature/your-feature-name
   git push origin --delete feature/your-feature-name
   ```

2. **Update your local main**:
   ```bash
   git checkout main
   git pull upstream main
   ```

## Code Review Process

### For Contributors

- Be patient - reviews may take time
- Be receptive to feedback
- Ask questions if feedback is unclear
- Make requested changes promptly

### For Reviewers

- Be respectful and constructive
- Explain the reasoning behind suggestions
- Approve when ready or request changes
- Test changes if possible

## Release Process

Maintainers handle releases:

1. Update version in `setup.py`
2. Update CHANGELOG.md
3. Create git tag
4. Push to GitHub
5. Create GitHub release
6. Announce release

## Getting Help

- **Documentation**: Check existing docs first
- **Discussions**: Use GitHub Discussions for questions
- **Issues**: For bugs or feature requests
- **Chat**: [Add Discord/Slack link if available]

## Recognition

Contributors are recognized in:
- GitHub contributors list
- CHANGELOG.md for significant contributions
- README.md acknowledgments section

## License

By contributing, you agree that your contributions will be licensed under the same license as the project (MIT License).

---

**Thank you for contributing to LumiGrid Display Node!** 🎉

Your contributions help make this project better for everyone.
