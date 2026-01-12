"""Tests for the setup wizard functionality."""

import pytest
from unittest.mock import MagicMock, patch


@pytest.fixture
def mock_controller_incomplete_setup():
    """Create a mock application controller with incomplete setup."""
    controller = MagicMock()
    controller.config = MagicMock()
    controller.config.is_setup_complete.return_value = False
    controller.config.get.return_value = {}
    controller.config.mark_setup_complete.return_value = True
    return controller


@pytest.fixture
def mock_controller_complete_setup():
    """Create a mock application controller with complete setup."""
    controller = MagicMock()
    controller.config = MagicMock()
    controller.config.is_setup_complete.return_value = True
    controller.config.get.return_value = {}
    return controller


@pytest.fixture
def client_incomplete_setup(mock_controller_incomplete_setup):
    """Create a test client with incomplete setup."""
    from lumigrid.web.app import WebInterface
    
    web_interface = WebInterface({}, mock_controller_incomplete_setup)
    web_interface.app.testing = True
    with web_interface.app.test_client() as client:
        yield client


@pytest.fixture
def client_complete_setup(mock_controller_complete_setup):
    """Create a test client with complete setup."""
    from lumigrid.web.app import WebInterface
    
    web_interface = WebInterface({}, mock_controller_complete_setup)
    web_interface.app.testing = True
    with web_interface.app.test_client() as client:
        yield client


class TestSetupWizard:
    """Tests for the setup wizard."""
    
    def test_redirect_to_setup_wizard_when_incomplete(self, client_incomplete_setup):
        """Test that root redirects to setup wizard when setup is incomplete."""
        response = client_incomplete_setup.get('/', follow_redirects=False)
        assert response.status_code == 302
        assert '/setup-wizard' in response.location
    
    def test_setup_wizard_page_accessible(self, client_incomplete_setup):
        """Test that setup wizard page is accessible when setup is incomplete."""
        response = client_incomplete_setup.get('/setup-wizard')
        assert response.status_code == 200
        assert b'Welcome to Lumigrid' in response.data
    
    def test_no_redirect_when_setup_complete(self, client_complete_setup):
        """Test that there's no redirect when setup is complete."""
        response = client_complete_setup.get('/', follow_redirects=False)
        assert response.status_code == 200
        # Should not redirect
    
    def test_wizard_network_info_endpoint(self, client_incomplete_setup):
        """Test the wizard network info endpoint."""
        with patch('socket.socket') as mock_socket_class, \
             patch('socket.gethostname', return_value='test-host'), \
             patch('subprocess.run') as mock_run:
            # Mock socket instance
            mock_socket = mock_socket_class.return_value
            mock_socket.getsockname.return_value = ('192.168.1.100', 0)
            
            # Mock subprocess for nmcli
            mock_run.return_value.returncode = 1  # Simulate nmcli not available or error
            
            response = client_incomplete_setup.get('/api/wizard/network-info')
            assert response.status_code == 200
            data = response.get_json()
            assert 'hostname' in data
            assert 'ip' in data
            assert 'connection' in data
            assert data['hostname'] == 'test-host'
    
    def test_wizard_save_endpoint(self, client_incomplete_setup, mock_controller_incomplete_setup):
        """Test the wizard save endpoint."""
        config_data = {
            'network': {
                'hostname': 'test-node',
                'node_id': 'test-id'
            },
            'control': {
                'mode': 'independent'
            },
            'led_matrix': {
                'rows': 32,
                'cols': 64
            }
        }
        
        response = client_incomplete_setup.post(
            '/api/wizard/save',
            json=config_data,
            content_type='application/json'
        )
        
        assert response.status_code == 200
        data = response.get_json()
        assert data['success'] is True
        
        # Verify mark_setup_complete was called
        mock_controller_incomplete_setup.config.mark_setup_complete.assert_called_once()
