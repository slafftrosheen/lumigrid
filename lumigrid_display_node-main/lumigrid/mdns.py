"""mDNS service advertising for LumiGrid Display Node."""

import socket
import logging
from typing import Optional

from zeroconf import ServiceInfo, Zeroconf

logger = logging.getLogger(__name__)


class MDNSService:
    """mDNS service for automatic discovery by other LumiGrid nodes."""

    def __init__(self, node_id: str, port: int = 5000):
        """Initialize mDNS service.

        Args:
            node_id: Unique identifier for this node
            port: Port number where the service is running
        """
        self.node_id = node_id
        self.port = port
        self.zeroconf: Optional[Zeroconf] = None
        self.service_info: Optional[ServiceInfo] = None

    def start(self):
        """Start advertising the mDNS service."""
        try:
            self.zeroconf = Zeroconf()
            self.service_info = ServiceInfo(
                "_lumigrid-node._tcp.local.",
                f"{self.node_id}._lumigrid-node._tcp.local.",
                addresses=[socket.inet_aton(self._get_ip())],
                port=self.port,
                properties={
                    'ver': '1.0.0',
                    'hw': 'rpi',
                    'type': 'display'
                }
            )
            self.zeroconf.register_service(self.service_info)
            logger.info(f"mDNS service started: {self.node_id} on port {self.port}")
        except Exception as e:
            logger.error(f"Failed to start mDNS service: {e}")
            if self.zeroconf:
                self.zeroconf.close()
                self.zeroconf = None
            self.service_info = None

    def stop(self):
        """Stop advertising the mDNS service."""
        try:
            if self.zeroconf and self.service_info:
                self.zeroconf.unregister_service(self.service_info)
                self.zeroconf.close()
                logger.info("mDNS service stopped")
        except Exception as e:
            logger.error(f"Error stopping mDNS service: {e}")
        finally:
            self.zeroconf = None
            self.service_info = None

    def _get_ip(self) -> str:
        """Get the local IP address.

        Returns:
            Local IP address as a string
        """
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            # Connect to a public DNS server to determine local IP
            # This doesn't actually send any data
            s.connect(('8.8.8.8', 80))
            return s.getsockname()[0]
        finally:
            s.close()
