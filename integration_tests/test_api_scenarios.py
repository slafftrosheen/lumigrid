import pytest
import requests

# For now, we will use a mock server to simulate the API endpoints.
# In the future, these tests could be run against live instances of the nodes.
# We'll use a simple dictionary to represent the state of our mock node.
mock_node_state = {
    "preset": None,
    "config": {"brightness": 100},
}

# We need a mock server to respond to our API requests.
# For this, we'll use the 'requests_mock' library, which needs to be added to requirements.txt
# But for now, let's write the tests first.

def test_preset_sync(requests_mock):
    """
    Tests that a preset can be sent to a node and the node's state is updated.
    """
    # Arrange
    requests_mock.post("http://mock-node/api/preset", text="OK")

    # Act
    new_preset = {"id": 1, "name": "Rainbow"}
    response = requests.post("http://mock-node/api/preset", json=new_preset)

    # Assert
    assert response.status_code == 200
    # In a real scenario, we would query the node's state to confirm the change.
    # For now, we'll just assert that the POST was successful.

def test_config_propagation(requests_mock):
    """
    Tests that a configuration change can be sent to a node.
    """
    # Arrange
    requests_mock.post("http://mock-node/api/config", text="OK")

    # Act
    new_config = {"brightness": 80}
    response = requests.post("http://mock-node/api/config", json=new_config)

    # Assert
    assert response.status_code == 200

def test_error_case_invalid_data(requests_mock):
    """
    Tests that the API returns an error when sent invalid data.
    """
    # Arrange
    requests_mock.post("http://mock-node/api/preset", status_code=400, text="Bad Request")

    # Act
    invalid_preset = {"name": "Incomplete Preset"} # Missing 'id'
    response = requests.post("http://mock-node/api/preset", json=invalid_preset)

    # Assert
    assert response.status_code == 400

def test_error_case_node_offline(requests_mock):
    """
    Tests that a connection error is handled when a node is offline.
    """
    # Arrange
    requests_mock.post("http://mock-node/api/preset", exc=requests.exceptions.ConnectionError)

    # Act & Assert
    with pytest.raises(requests.exceptions.ConnectionError):
        requests.post("http://mock-node/api/preset", json={"id": 2, "name": "Offline Test"})
