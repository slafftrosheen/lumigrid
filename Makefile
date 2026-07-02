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

install: install-lander

install-lander:
	@echo "Installing dependencies for lumigrid-lander-main..."
	cd lumigrid-lander-main && npm install

test: test-lander

test-lander:
	@echo "Running tests for lumigrid-lander-main..."
	cd lumigrid-lander-main && npm run test:unit

clean:
	@echo "Cleaning up..."
	rm -rf lumigrid-lander-main/node_modules
	rm -rf lumigrid-lander-main/build
