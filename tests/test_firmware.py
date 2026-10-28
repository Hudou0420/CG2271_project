import os
import unittest
from native_support import native, json_include, fixture_secrets


class FirmwareTests(unittest.TestCase):
    def test_mcu_game_task_deadlines(self):
        native("mcu_game_task", ["tests/test_mcu_game_task.c", "tests/fakes/runtime/runtime.c", "source/vault_fsm.c"],
               includes=["tests/fakes/mcu", "tests/fakes/runtime", "source", "bsp"])

    def test_production_cloud_runtime(self):
        native("cloud_runtime", ["tests/test_cloud_runtime.cpp", "tests/fakes/runtime/runtime.c", "esp32/src/ai.cpp",
                                 "esp32/src/ai_policy.cpp", "esp32/src/ai_response.cpp", "esp32/src/vault_state.cpp",
                                 "esp32/src/https_guard.cpp", "esp32/src/https_client.cpp", "common/vault_protocol/vault_protocol.c"],
               cxx=True, includes=["tests/fakes/esp32", "tests/fakes/runtime", "esp32/src", "esp32/include", "common/vault_protocol", json_include(), fixture_secrets()])

    def test_production_ai_runtime(self):
        native("ai_runtime", ["tests/test_ai_runtime.cpp", "tests/fakes/runtime/runtime.c",
                              "esp32/src/ai_policy.cpp", "esp32/src/ai_response.cpp", "esp32/src/https_guard.cpp", "esp32/src/https_client.cpp"],
               cxx=True, includes=["tests/fakes/esp32", "tests/fakes/runtime", "esp32/src", "common/vault_protocol", json_include(), fixture_secrets()])

    def test_mcu_production_sensor_service(self):
        for beam in (0, 1):
            with self.subTest(beam=beam):
                native("mcu_sensors_" + str(beam), ["tests/test_mcu_sensors.c", "tests/fakes/runtime/runtime.c", "tests/fakes/mcu/hardware.c",
                                                  "source/vault_fsm.c", "source/vault_sensor_logic.c"],
                       includes=["tests/fakes/mcu", "tests/fakes/runtime", "source", "bsp"],
                       defines=["VAULT_ENCODER_STEPS_PER_ROUND=1", "VAULT_LDR_BEAM_DETECTION_ENABLED=" + str(beam)])

    def test_esp32_production_uart(self):
        native("esp32_link", ["tests/test_esp32_link.cpp", "tests/fakes/runtime/runtime.c", "common/vault_protocol/vault_protocol.c"],
               cxx=True, includes=["tests/fakes/esp32", "tests/fakes/runtime", "esp32/include", "common/vault_protocol"])

    def test_mcu_production_uart(self):
        native("mcu_comms", ["tests/test_mcu_comms.c", "tests/fakes/runtime/runtime.c", "tests/fakes/mcu/hardware.c",
                              "source/vault_fsm.c", "common/vault_protocol/vault_protocol.c"],
               includes=["tests/fakes/mcu", "tests/fakes/runtime", "source", "bsp", "common/vault_protocol"])

    def test_full_game_and_boundaries(self):
        native("fsm", ["tests/test_fsm.c", "source/vault_fsm.c"], includes=["source"])

    def test_existing_sensor_regressions(self):
        native("sensor_logic", ["tests/test_sensor_logic.c", "source/vault_sensor_logic.c", "source/vault_fsm.c"], includes=["source"])

    def test_seeded_protocol_and_game_properties(self):
        native("properties", ["tests/test_properties.c", "source/vault_fsm.c", "common/vault_protocol/vault_protocol.c"],
               includes=["source", "common/vault_protocol"],
               arguments=[os.getenv("VAULT_TEST_SEED", "8817"), os.getenv("VAULT_TEST_ITERATIONS", "1000")])
