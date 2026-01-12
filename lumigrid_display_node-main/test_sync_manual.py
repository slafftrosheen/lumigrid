#!/usr/bin/env python3
"""Manual test script for UDP synchronization.

This script allows you to test the sync protocol by simulating
a master or slave node and observing the logging output.

Usage:
    python test_sync_manual.py master   # Run as master
    python test_sync_manual.py slave    # Run as slave
"""

import sys
import time
import logging
from lumigrid.sync import SyncProtocol, ClockState

# Setup logging to see all sync events
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    datefmt='%H:%M:%S'
)

logger = logging.getLogger(__name__)


def run_master(duration=30):
    """Run as sync master for specified duration.
    
    Args:
        duration: How long to run in seconds (default: 30)
    """
    logger.info("Starting sync master test")
    logger.info(f"Will broadcast for {duration} seconds")
    
    sync = SyncProtocol()
    sync.start_master()
    
    try:
        start_time = time.time()
        while time.time() - start_time < duration:
            # Print status every 5 seconds
            if int(time.time() - start_time) % 5 == 0:
                status = sync.get_status()
                logger.info(f"Master status: {status}")
            time.sleep(1)
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
    finally:
        sync.stop()
        logger.info("Master test complete")


def run_slave(duration=30):
    """Run as sync slave for specified duration.
    
    Args:
        duration: How long to listen in seconds (default: 30)
    """
    logger.info("Starting sync slave test")
    logger.info(f"Will listen for {duration} seconds")
    
    sync = SyncProtocol()
    
    # Track received ticks
    tick_count = 0
    
    def on_tick(timestamp_ms, drift_ewma):
        nonlocal tick_count
        tick_count += 1
        # Log every 20 ticks (approximately 1 second at standard packet rate)
        if tick_count % 20 == 0:
            logger.info(f"Received tick #{tick_count}: timestamp={timestamp_ms}, drift={drift_ewma:.1f}ms")
    
    sync.on_tick(on_tick)
    sync.start_slave()
    
    try:
        start_time = time.time()
        while time.time() - start_time < duration:
            # Print status every 5 seconds
            if int(time.time() - start_time) % 5 == 0:
                status = sync.get_status()
                logger.info(f"Slave status: {status}")
            time.sleep(1)
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
    finally:
        sync.stop()
        logger.info(f"Slave test complete - received {tick_count} ticks")


def main():
    if len(sys.argv) < 2:
        print("Usage: python test_sync_manual.py [master|slave] [duration_seconds]")
        print("\nExamples:")
        print("  python test_sync_manual.py master 60    # Run master for 60 seconds")
        print("  python test_sync_manual.py slave 30     # Run slave for 30 seconds")
        sys.exit(1)
    
    mode = sys.argv[1].lower()
    duration = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    
    if mode == 'master':
        run_master(duration)
    elif mode == 'slave':
        run_slave(duration)
    else:
        print(f"Unknown mode: {mode}")
        print("Must be 'master' or 'slave'")
        sys.exit(1)


if __name__ == '__main__':
    main()
