.PHONY: all install test clean help

help:
	@echo "LumiGrid Monorepo Management"
	@echo ""
	@echo "Usage:"
	@echo "  make install       Install dependencies for all sub-projects"
	@echo "  make test          Run tests for all sub-projects"
	@echo "  make clean         Clean build artifacts"
	@echo ""
	@echo "Sub-projects:"
	@echo "  - lumigrid-lander-main (Frontend)"
	@echo "  - lumigrid_display_node-main (Display Node)"
	@echo "  - lumigrid_led_node-main (LED Node)"
	@echo "  - lumigrid_misc-main (Schemas & Examples)"

install: install-lander install-display install-misc

install-lander:
	@echo "Installing dependencies for lumigrid-lander-main..."
	cd lumigrid-lander-main && npm install

install-display:
	@echo "Installing dependencies for lumigrid_display_node-main..."
	cd lumigrid_display_node-main && pip install -r requirements.txt

install-misc:
	@echo "Installing dependencies for lumigrid_misc-main examples..."
	cd lumigrid_misc-main/examples/nodejs && npm install
	cd lumigrid_misc-main/examples/python && pip install -r requirements.txt

test: test-lander test-display

test-lander:
	@echo "Running tests for lumigrid-lander-main..."
	cd lumigrid-lander-main && npm run test

test-display:
	@echo "Running tests for lumigrid_display_node-main..."
	cd lumigrid_display_node-main && pytest

clean:
	@echo "Cleaning up..."
	rm -rf lumigrid-lander-main/node_modules
	rm -rf lumigrid-lander-main/build
	rm -rf lumigrid_display_node-main/__pycache__
	rm -rf lumigrid_display_node-main/venv
