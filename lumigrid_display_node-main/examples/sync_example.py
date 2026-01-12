#!/usr/bin/env python3
"""
Example: UDP Synchronization Usage

This example demonstrates how to use the UDP sync protocol
to coordinate multiple display nodes.
"""

import time
import logging
from lumigrid.sync import SyncProtocol, ClockState

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)

logger = logging.getLogger(__name__)


def example_master():
    """Example of running as sync master."""
    logger.info("=== Master Node Example ===")
    
    # Create sync protocol instance
    sync = SyncProtocol()
    
    # Start broadcasting as master
    sync.start_master()
    logger.info("Broadcasting sync packets at 20 Hz...")
    
    try:
        # Run for 10 seconds
        for i in range(10):
            time.sleep(1)
            status = sync.get_status()
            logger.info(f"Status: {status['packets_sent']} packets sent")
    finally:
        # Always clean up
        sync.stop()
        logger.info("Master stopped")


def example_slave():
    """Example of running as sync slave."""
    logger.info("=== Slave Node Example ===")
    
    # Create sync protocol instance
    sync = SyncProtocol()
    
    # Define callback for tick events
    def handle_tick(timestamp_ms, drift_ewma):
        """Called when a sync tick is received."""
        logger.info(f"Tick received: timestamp={timestamp_ms}, drift={drift_ewma:.1f}ms")
    
    # Register the callback
    sync.on_tick(handle_tick)
    
    # Start listening as slave
    sync.start_slave()
    logger.info("Listening for sync packets...")
    
    try:
        # Monitor for 10 seconds
        start_time = time.time()
        while time.time() - start_time < 10:
            time.sleep(1)
            status = sync.get_status()
            logger.info(f"Clock state: {status['clock_state']}, "
                       f"Packets: {status['packets_received']}, "
                       f"Drift: {status['drift_ewma_ms']:.1f}ms")
    finally:
        sync.stop()
        logger.info("Slave stopped")


def example_monitoring():
    """Example of monitoring sync status."""
    logger.info("=== Monitoring Example ===")
    
    sync = SyncProtocol()
    
    # Track state changes
    last_state = None
    
    def handle_tick(timestamp_ms, drift_ewma):
        nonlocal last_state
        
        current_state = sync.clock_state
        
        # Detect state changes
        if current_state != last_state:
            logger.warning(f"State changed: {last_state} -> {current_state}")
            last_state = current_state
        
        # Alert on high drift
        if abs(drift_ewma) > 50:
            logger.warning(f"High drift detected: {drift_ewma:.1f}ms")
    
    sync.on_tick(handle_tick)
    sync.start_slave()
    
    try:
        time.sleep(10)
    finally:
        sync.stop()


if __name__ == '__main__':
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: python sync_example.py [master|slave|monitor]")
        sys.exit(1)
    
    mode = sys.argv[1].lower()
    
    if mode == 'master':
        example_master()
    elif mode == 'slave':
        example_slave()
    elif mode == 'monitor':
        example_monitoring()
    else:
        print(f"Unknown mode: {mode}")
        sys.exit(1)
