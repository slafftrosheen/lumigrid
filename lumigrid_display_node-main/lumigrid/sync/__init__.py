"""Sync module for Master/Slave synchronization."""

from .protocol import SyncProtocol, ClockState

__all__ = ['SyncProtocol', 'ClockState']
