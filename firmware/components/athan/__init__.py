"""athan: version 3 sounds, radio, yearly prayer times and the /audio page (see athan.h)."""

import esphome.codegen as cg
from esphome.components import audio, esp32, text
from esphome.components import time as time_
from esphome.components.speaker.media_player import SpeakerMediaPlayer
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIME_ID

CODEOWNERS = ["@et7ad"]
DEPENDENCIES = ["esp32", "network", "psram", "web_server_base", "text"]
AUTO_LOAD = ["json", "audio"]

CONF_MEDIA_PLAYER = "media_player"
CONF_DATA_URL = "data_url"
CONF_RADIO_URLS = "radio_urls"

athan_ns = cg.esphome_ns.namespace("athan")
AthanComponent = athan_ns.class_("AthanComponent", cg.Component)


def _request_codecs(config):
    # Stored sounds are MP3; make sure the decoder is compiled in whatever the pipelines request.
    audio.request_mp3_support()
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AthanComponent),
            cv.Required(CONF_MEDIA_PLAYER): cv.use_id(SpeakerMediaPlayer),
            cv.Required(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
            # Base URL of this repository's docs/ folder (catalog, stations, yearly prayer files).
            cv.Optional(
                CONF_DATA_URL,
                default="https://raw.githubusercontent.com/et7ad/esp32_athan/main/docs",
            ): cv.url,
            # Own links of radio slots 1..10 (template text entities), in slot order.
            cv.Optional(CONF_RADIO_URLS, default=[]): cv.All(
                cv.ensure_list(cv.use_id(text.Text)), cv.Length(max=10)
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    _request_codecs,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    player = await cg.get_variable(config[CONF_MEDIA_PLAYER])
    cg.add(var.set_media_player(player))
    clock = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_time(clock))
    cg.add(var.set_data_url(config[CONF_DATA_URL].rstrip("/")))
    for text_id in config[CONF_RADIO_URLS]:
        cg.add(var.add_radio_url(await cg.get_variable(text_id)))
    # HTTPS downloads with certificate checks (raw.githubusercontent.com).
    esp32.require_certificate_bundle()
    esp32.include_builtin_idf_component("esp_http_client")
    esp32.include_builtin_idf_component("esp-tls")
