.PHONY: build clean

PLUGIN_NAME = beacon_listener_graph.so

build:
	GOEXPERIMENT=jsonv2,greenteagc CGO_ENABLED=1 go build -buildmode=plugin -o $(PLUGIN_NAME)

clean:
	rm -f $(PLUGIN_NAME)
