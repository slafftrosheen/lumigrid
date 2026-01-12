#!/usr/bin/env python3
"""WSGI entry point for production deployment with Gunicorn/uWSGI."""

import os
import sys
import logging
import logging.handlers

from lumigrid.config import Config
from lumigrid.controller import ApplicationController
from lumigrid.web.app import WebInterface


def setup_logging(config: Config):
    """Setup logging configuration.

    Args:
        config: Configuration object
    """
    log_level = config.get('logging.level', 'INFO')
    log_file = config.get('logging.file', 'logs/lumigrid.log')
    max_bytes = config.get('logging.max_bytes', 10485760)
    backup_count = config.get('logging.backup_count', 5)

    # Create logs directory if it doesn't exist
    log_dir = os.path.dirname(log_file)
    if log_dir and not os.path.exists(log_dir):
        os.makedirs(log_dir)

    # Configure root logger
    logger = logging.getLogger()
    logger.setLevel(getattr(logging, log_level))

    # Console handler
    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(getattr(logging, log_level))
    console_format = logging.Formatter(
        '%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    )
    console_handler.setFormatter(console_format)
    logger.addHandler(console_handler)

    # File handler (rotating)
    try:
        file_handler = logging.handlers.RotatingFileHandler(
            log_file, maxBytes=max_bytes, backupCount=backup_count
        )
        file_handler.setLevel(getattr(logging, log_level))
        file_format = logging.Formatter(
            '%(asctime)s - %(name)s - %(levelname)s - %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
        file_handler.setFormatter(file_format)
        logger.addHandler(file_handler)
    except Exception as e:
        logger.warning(f"Could not setup file logging: {e}")


# Load configuration
config_path = os.environ.get('LUMIGRID_CONFIG', 'config.yaml')
config = Config(config_path)

# Setup logging
setup_logging(config)
logger = logging.getLogger(__name__)

logger.info("=" * 50)
logger.info("Initializing Lumigrid Display Node (WSGI)")
logger.info("=" * 50)

# Create application controller
controller = ApplicationController(config_path)
controller.start()

# Create web interface
web_config = config.get('web', {})
web_interface = WebInterface(web_config, controller)

# Export the Flask app for WSGI
app = web_interface.app

logger.info("Lumigrid Display Node WSGI app initialized")
