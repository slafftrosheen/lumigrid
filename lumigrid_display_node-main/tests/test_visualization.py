"""Tests for the visualization module."""

import pytest
from unittest.mock import MagicMock
from PIL import Image

from lumigrid.display.visualization import (
    ChartStyle,
    DataPoint,
    DataSeries,
    AlertThreshold,
    ChartBase,
    AnimatedBarChart,
    RealtimeLineGraph,
    AnimatedGauge,
    HeatMap,
    ScatterPlot,
    RadarChart,
    DataSource,
    SensorDataSource,
    AlertManager,
    VisualizationManager,
)


class TestDataPoint:
    """Tests for DataPoint class."""
    
    def test_creation_minimal(self):
        """Test data point creation with minimal args."""
        point = DataPoint(value=42.0)
        
        assert point.value == 42.0
        assert point.label == ""
    
    def test_creation_full(self):
        """Test data point creation with all args."""
        point = DataPoint(
            value=42.0,
            label="Test",
            timestamp=1234567890.0,
            color=(255, 0, 0)
        )
        
        assert point.value == 42.0
        assert point.label == "Test"
        assert point.timestamp == 1234567890.0
        assert point.color == (255, 0, 0)


class TestDataSeries:
    """Tests for DataSeries class."""
    
    def test_creation(self):
        """Test data series creation."""
        series = DataSeries(name="Temperature", color=(255, 0, 0))
        
        assert series.name == "Temperature"
        assert series.color == (255, 0, 0)
        assert len(series.points) == 0
    
    def test_add_point(self):
        """Test adding points."""
        series = DataSeries(name="Test")
        series.add_point(10.0, "First")
        series.add_point(20.0, "Second")
        
        assert len(series.points) == 2
        assert series.points[0].value == 10.0
    
    def test_get_values(self):
        """Test getting all values."""
        series = DataSeries(name="Test")
        series.add_point(10.0)
        series.add_point(20.0)
        series.add_point(30.0)
        
        values = series.get_values()
        
        assert values == [10.0, 20.0, 30.0]
    
    def test_get_min_max(self):
        """Test getting min/max values."""
        series = DataSeries(name="Test")
        series.add_point(10.0)
        series.add_point(5.0)
        series.add_point(25.0)
        
        min_val, max_val = series.get_min_max()
        
        assert min_val == 5.0
        assert max_val == 25.0


class TestAlertThreshold:
    """Tests for AlertThreshold class."""
    
    def test_creation(self):
        """Test alert threshold creation."""
        alert = AlertThreshold(
            name="High Temp",
            condition="above",
            value=30.0,
            color=(255, 0, 0)
        )
        
        assert alert.name == "High Temp"
        assert alert.condition == "above"
        assert alert.value == 30.0
    
    def test_check_above(self):
        """Test 'above' condition check."""
        alert = AlertThreshold(name="High", condition="above", value=30.0)
        
        assert alert.check(35.0) is True
        assert alert.check(25.0) is False
    
    def test_check_below(self):
        """Test 'below' condition check."""
        alert = AlertThreshold(name="Low", condition="below", value=10.0)
        
        assert alert.check(5.0) is True
        assert alert.check(15.0) is False
    
    def test_check_equal(self):
        """Test 'equal' condition check."""
        alert = AlertThreshold(name="Target", condition="equal", value=50.0)
        
        assert alert.check(50.0) is True
        assert alert.check(50.0005) is True  # Within tolerance
        assert alert.check(51.0) is False


class TestAnimatedBarChart:
    """Tests for AnimatedBarChart class."""
    
    def test_creation(self):
        """Test bar chart creation."""
        chart = AnimatedBarChart(width=64, height=32, data=[10, 20, 30])
        
        assert chart.width == 64
        assert chart.height == 32
        assert chart.data == [10, 20, 30]
    
    def test_set_data(self):
        """Test setting data."""
        chart = AnimatedBarChart(width=64, height=32)
        chart.set_data([5, 10, 15, 20])
        
        assert chart.data == [5, 10, 15, 20]
    
    def test_render(self):
        """Test chart rendering."""
        chart = AnimatedBarChart(width=64, height=32, data=[10, 20, 30, 40, 50])
        
        image = chart.render()
        
        assert image.size == (64, 32)
    
    def test_render_horizontal(self):
        """Test horizontal bar chart rendering."""
        chart = AnimatedBarChart(
            width=64, height=32,
            data=[10, 20, 30],
            horizontal=True
        )
        
        image = chart.render()
        
        assert image.size == (64, 32)


class TestRealtimeLineGraph:
    """Tests for RealtimeLineGraph class."""
    
    def test_creation(self):
        """Test line graph creation."""
        graph = RealtimeLineGraph(width=64, height=32)
        
        assert graph.width == 64
        assert graph.height == 32
        assert len(graph.series) == 0
    
    def test_add_series(self):
        """Test adding series."""
        graph = RealtimeLineGraph(width=64, height=32)
        series = graph.add_series("Temperature", (255, 0, 0))
        
        assert len(graph.series) == 1
        assert series.name == "Temperature"
    
    def test_add_point(self):
        """Test adding points to series."""
        graph = RealtimeLineGraph(width=64, height=32)
        graph.add_series("Test")
        
        graph.add_point(0, 25.0)
        graph.add_point(0, 26.0)
        
        assert len(graph.series[0].points) == 2
    
    def test_max_points_limit(self):
        """Test max points limit."""
        graph = RealtimeLineGraph(width=64, height=32, max_points=5)
        graph.add_series("Test")
        
        for i in range(10):
            graph.add_point(0, float(i))
        
        assert len(graph.series[0].points) == 5
    
    def test_render(self):
        """Test graph rendering."""
        graph = RealtimeLineGraph(width=64, height=32)
        series = graph.add_series("Test")
        for i in range(10):
            series.add_point(float(i * 10))
        
        image = graph.render()
        
        assert image.size == (64, 32)


class TestAnimatedGauge:
    """Tests for AnimatedGauge class."""
    
    def test_creation(self):
        """Test gauge creation."""
        gauge = AnimatedGauge(width=64, height=32, value=50)
        
        assert gauge.width == 64
        assert gauge.height == 32
        assert gauge.value == 50
    
    def test_value_clamping(self):
        """Test value clamping."""
        gauge = AnimatedGauge(width=64, height=32, min_value=0, max_value=100)
        
        gauge.value = 150
        assert gauge.value == 100
        
        gauge.value = -50
        assert gauge.value == 0
    
    def test_animation(self):
        """Test gauge animation."""
        gauge = AnimatedGauge(width=64, height=32, value=100)
        gauge._animated_value = 50  # Start at 50
        
        gauge.update(0.5)
        
        # Animation should have progressed towards 100
        assert gauge._animated_value > 50
    
    def test_render(self):
        """Test gauge rendering."""
        gauge = AnimatedGauge(width=64, height=32, value=75)
        
        image = gauge.render()
        
        assert image.size == (64, 32)


class TestHeatMap:
    """Tests for HeatMap class."""
    
    def test_creation(self):
        """Test heat map creation."""
        heat_map = HeatMap(width=64, height=32)
        
        assert heat_map.width == 64
        assert heat_map.height == 32
    
    def test_set_data(self):
        """Test setting data."""
        heat_map = HeatMap(width=64, height=32)
        data = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
        
        heat_map.set_data(data)
        
        assert heat_map.data == data
    
    def test_render(self):
        """Test heat map rendering."""
        data = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
        heat_map = HeatMap(width=64, height=32, data=data)
        
        image = heat_map.render()
        
        assert image.size == (64, 32)
    
    def test_color_mapping(self):
        """Test color mapping."""
        heat_map = HeatMap(width=64, height=32)
        
        color = heat_map._value_to_color(50, 0, 100)
        
        assert len(color) == 3
        assert all(0 <= c <= 255 for c in color)


class TestScatterPlot:
    """Tests for ScatterPlot class."""
    
    def test_creation(self):
        """Test scatter plot creation."""
        scatter = ScatterPlot(width=64, height=32)
        
        assert scatter.width == 64
        assert scatter.height == 32
        assert len(scatter.points) == 0
    
    def test_add_point(self):
        """Test adding points."""
        scatter = ScatterPlot(width=64, height=32)
        scatter.add_point(10.0, 20.0)
        scatter.add_point(30.0, 40.0)
        
        assert len(scatter.points) == 2
    
    def test_set_points(self):
        """Test setting points."""
        scatter = ScatterPlot(width=64, height=32)
        points = [(1, 2), (3, 4), (5, 6)]
        
        scatter.set_points(points)
        
        assert len(scatter.points) == 3
    
    def test_trend_line_calculation(self):
        """Test trend line calculation."""
        scatter = ScatterPlot(width=64, height=32, show_trend=True)
        scatter.set_points([(0, 0), (1, 1), (2, 2)])
        
        trend = scatter._calculate_trend_line()
        
        assert trend is not None
        slope, intercept = trend
        assert abs(slope - 1.0) < 0.1  # Should be approximately 1
    
    def test_render(self):
        """Test scatter plot rendering."""
        scatter = ScatterPlot(width=64, height=32)
        scatter.set_points([(10, 20), (30, 40), (50, 60)])
        
        image = scatter.render()
        
        assert image.size == (64, 32)


class TestRadarChart:
    """Tests for RadarChart class."""
    
    def test_creation(self):
        """Test radar chart creation."""
        radar = RadarChart(width=64, height=32)
        
        assert radar.width == 64
        assert radar.height == 32
    
    def test_set_data(self):
        """Test setting data."""
        radar = RadarChart(width=64, height=32)
        categories = ["A", "B", "C", "D", "E"]
        values = [80, 60, 90, 70, 50]
        
        radar.set_data(categories, values)
        
        assert radar.categories == categories
        assert radar.values == values
    
    def test_render(self):
        """Test radar chart rendering."""
        radar = RadarChart(
            width=64, height=32,
            categories=["A", "B", "C", "D", "E"],
            values=[80, 60, 90, 70, 50]
        )
        
        image = radar.render()
        
        assert image.size == (64, 32)


class TestAlertManager:
    """Tests for AlertManager class."""
    
    def test_creation(self):
        """Test alert manager creation."""
        manager = AlertManager()
        
        assert len(manager.alerts) == 0
    
    def test_add_alert(self):
        """Test adding alerts."""
        manager = AlertManager()
        alert = AlertThreshold(name="High Temp", condition="above", value=30.0)
        
        manager.add_alert(alert)
        
        assert "High Temp" in manager.alerts
    
    def test_check_value(self):
        """Test checking values against alerts."""
        manager = AlertManager()
        manager.add_alert(AlertThreshold(name="High", condition="above", value=30.0))
        
        triggered = manager.check_value(35.0)
        
        assert len(triggered) == 1
        assert triggered[0].name == "High"
    
    def test_alert_callback(self):
        """Test alert callback."""
        manager = AlertManager()
        manager.add_alert(AlertThreshold(name="High", condition="above", value=30.0))
        
        alerts_triggered = []
        manager.on_alert(lambda alert, value, source: alerts_triggered.append(alert.name))
        
        manager.check_value(35.0)
        
        assert len(alerts_triggered) == 1
    
    def test_get_active_alerts(self):
        """Test getting active alerts."""
        manager = AlertManager()
        manager.add_alert(AlertThreshold(name="High", condition="above", value=30.0))
        manager.add_alert(AlertThreshold(name="Low", condition="below", value=10.0))
        
        manager.check_value(35.0)  # Triggers High
        
        active = manager.get_active_alerts()
        
        assert len(active) == 1
        assert active[0].name == "High"


class TestVisualizationManager:
    """Tests for VisualizationManager class."""
    
    def test_creation(self):
        """Test manager creation."""
        manager = VisualizationManager(width=64, height=32)
        
        assert manager.width == 64
        assert manager.height == 32
    
    def test_add_chart(self):
        """Test adding charts."""
        manager = VisualizationManager(width=64, height=32)
        chart = AnimatedBarChart(width=64, height=32, data=[10, 20, 30])
        
        manager.add_chart("bar", chart)
        
        assert "bar" in manager.charts
        assert manager._active_chart == "bar"
    
    def test_set_active_chart(self):
        """Test setting active chart."""
        manager = VisualizationManager(width=64, height=32)
        manager.add_chart("bar", AnimatedBarChart(64, 32, data=[1, 2, 3]))
        manager.add_chart("gauge", AnimatedGauge(64, 32, value=50))
        
        result = manager.set_active_chart("gauge")
        
        assert result is True
        assert manager._active_chart == "gauge"
    
    def test_render(self):
        """Test rendering active chart."""
        manager = VisualizationManager(width=64, height=32)
        manager.add_chart("bar", AnimatedBarChart(64, 32, data=[10, 20, 30]))
        
        image = manager.render()
        
        assert image.size == (64, 32)
    
    def test_render_no_charts(self):
        """Test rendering with no charts."""
        manager = VisualizationManager(width=64, height=32)
        
        image = manager.render()
        
        assert image.size == (64, 32)
