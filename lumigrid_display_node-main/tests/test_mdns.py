"""Tests for the mDNS service module."""

import pytest
import socket
from unittest.mock import Mock, patch, MagicMock
from lumigrid.mdns import MDNSService


class TestMDNSService:
    """Tests for MDNSService class."""

    def test_init(self):
        """Test MDNSService initialization."""
        service = MDNSService("test-node", 5000)
        assert service.node_id == "test-node"
        assert service.port == 5000
        assert service.zeroconf is None
        assert service.service_info is None

    def test_init_default_port(self):
        """Test MDNSService initialization with default port."""
        service = MDNSService("test-node")
        assert service.node_id == "test-node"
        assert service.port == 5000

    @patch('lumigrid.mdns.Zeroconf')
    @patch('lumigrid.mdns.ServiceInfo')
    @patch('lumigrid.mdns.socket.inet_aton')
    def test_start(self, mock_inet_aton, mock_service_info, mock_zeroconf):
        """Test starting mDNS service."""
        # Setup mocks
        mock_inet_aton.return_value = b'\xc0\xa8\x01\x64'  # 192.168.1.100
        mock_zeroconf_instance = Mock()
        mock_zeroconf.return_value = mock_zeroconf_instance
        mock_service_info_instance = Mock()
        mock_service_info.return_value = mock_service_info_instance

        service = MDNSService("test-node", 5000)
        
        with patch.object(service, '_get_ip', return_value='192.168.1.100'):
            service.start()

        # Verify Zeroconf was initialized
        mock_zeroconf.assert_called_once()
        assert service.zeroconf == mock_zeroconf_instance

        # Verify ServiceInfo was created with correct parameters
        mock_service_info.assert_called_once_with(
            "_lumigrid-node._tcp.local.",
            "test-node._lumigrid-node._tcp.local.",
            addresses=[b'\xc0\xa8\x01\x64'],
            port=5000,
            properties={
                'ver': '1.0.0',
                'hw': 'rpi',
                'type': 'display'
            }
        )
        assert service.service_info == mock_service_info_instance

        # Verify service was registered
        mock_zeroconf_instance.register_service.assert_called_once_with(
            mock_service_info_instance
        )

    @patch('lumigrid.mdns.Zeroconf')
    def test_start_exception_handling(self, mock_zeroconf):
        """Test that start handles exceptions gracefully."""
        mock_zeroconf.side_effect = Exception("Test error")
        
        service = MDNSService("test-node", 5000)
        service.start()

        # Should not raise, but service should be None
        assert service.zeroconf is None
        assert service.service_info is None

    def test_stop_when_not_started(self):
        """Test stopping service when it was never started."""
        service = MDNSService("test-node", 5000)
        service.stop()  # Should not raise

        assert service.zeroconf is None
        assert service.service_info is None

    @patch('lumigrid.mdns.Zeroconf')
    @patch('lumigrid.mdns.ServiceInfo')
    @patch('lumigrid.mdns.socket.inet_aton')
    def test_stop(self, mock_inet_aton, mock_service_info, mock_zeroconf):
        """Test stopping mDNS service."""
        mock_inet_aton.return_value = b'\xc0\xa8\x01\x64'
        mock_zeroconf_instance = Mock()
        mock_zeroconf.return_value = mock_zeroconf_instance
        mock_service_info_instance = Mock()
        mock_service_info.return_value = mock_service_info_instance

        service = MDNSService("test-node", 5000)
        
        with patch.object(service, '_get_ip', return_value='192.168.1.100'):
            service.start()
            service.stop()

        # Verify service was unregistered
        mock_zeroconf_instance.unregister_service.assert_called_once_with(
            mock_service_info_instance
        )
        # Verify zeroconf was closed
        mock_zeroconf_instance.close.assert_called_once()

        # Verify cleanup
        assert service.zeroconf is None
        assert service.service_info is None

    @patch('lumigrid.mdns.socket.socket')
    def test_get_ip(self, mock_socket_class):
        """Test getting local IP address."""
        mock_socket_instance = Mock()
        mock_socket_instance.getsockname.return_value = ('192.168.1.100', 12345)
        mock_socket_class.return_value = mock_socket_instance

        service = MDNSService("test-node", 5000)
        ip = service._get_ip()

        assert ip == '192.168.1.100'
        
        # Verify socket was used correctly
        mock_socket_class.assert_called_once_with(
            socket.AF_INET, socket.SOCK_DGRAM
        )
        mock_socket_instance.connect.assert_called_once_with(('8.8.8.8', 80))
        mock_socket_instance.close.assert_called_once()

    @patch('lumigrid.mdns.socket.socket')
    def test_get_ip_closes_socket_on_exception(self, mock_socket_class):
        """Test that _get_ip closes socket even on exception."""
        mock_socket_instance = Mock()
        mock_socket_instance.connect.side_effect = Exception("Connection failed")
        mock_socket_class.return_value = mock_socket_instance

        service = MDNSService("test-node", 5000)
        
        with pytest.raises(Exception):
            service._get_ip()

        # Verify socket was closed despite exception
        mock_socket_instance.close.assert_called_once()
