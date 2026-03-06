idf_component_register(
    SRCS
        "src/geo_utils.cpp"
        "src/duplicate_filter.cpp"
        "src/routing_engine.cpp"
        "src/forwarding_queue.cpp"
        "src/lora_link_adapter.cpp"
        "src/network_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES lora_radio
)
