#include "platform/esp32/web_ui.h"

#include <WiFi.h>
#include <WebServer.h>

#include "core/config.h"
#include "core/dc_motor.h"
#include "core/hardware_devices.h"
#include "core/led.h"
#include "core/pixels.h"
#include "core/relay.h"
#include "core/stepper.h"
#include "platform/esp32/config_storage.h"
#include "platform/esp32/dmx_sacn.h"
#include "platform/esp32/wifi_ota.h"

static WebServer server(80);

static String htmlHead(const String& title) {
  return String("<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>") + title + "</title></head>"
                "<body style='font-family:sans-serif;max-width:820px;margin:16px;'>";
}

static void handleRoot() {
  String s = htmlHead(deviceName());
  s += "<h2>" + deviceName() + "</h2>";
  s += "<p><b>Mode:</b> " + String((WiFi.getMode() & WIFI_AP) ? "AP" : "STA");
  if (WiFi.getMode() & WIFI_STA) s += " | <b>IP:</b> " + WiFi.localIP().toString();
  if (WiFi.getMode() & WIFI_AP)  s += " | <b>AP IP:</b> " + WiFi.softAPIP().toString();
  s += "</p>";

  s += "<h3>sACN Diagnostics</h3>";
  s += "<ul>";
  s += "<li>Universe configured: " + String(cfg.universe) + "</li>";
  s += "<li>Start Addr: " + String(cfg.startAddr) + " (uses 4 slots)</li>";
  s += "<li>Mode: " + String(cfg.sacnMode == SACN_UNICAST ? "UNICAST" : "MULTICAST") + "</li>";
  s += "<li>Packets: " + String(sacnPacketCounter()) + "</li>";
  s += "<li>Last universe seen: " + String(lastUniverseSeen()) + "</li>";
  s += "<li>Last DC raw16: " + String(lastDcRawValue()) + "</li>";
  s += "<li>Last Step raw16: " + String(lastStepRawValue()) + "</li>";
  s += "<li>DMX active: " + String(dmxActive() ? "yes" : "no") + "</li>";
  s += "</ul>";

  s += "<h3>Quick Tests</h3>";
  s += "<p><a href='/test/dc/fwd'>DC FWD</a> | "
       "<a href='/test/dc/rev'>DC REV</a> | "
       "<a href='/test/dc/stop'>DC STOP</a></p>";

  s += "<p><a href='/test/step/0'>Step 0°</a> | "
       "<a href='/test/step/90'>90°</a> | "
       "<a href='/test/step/180'>180°</a> | "
       "<a href='/test/step/270'>270°</a> | "
       "<a href='/test/step/360'>360°</a></p>";

  s += "<p><a href='/home'>Set Home (current as 0°)</a></p>";
  s += "<p><a href='/wifi'>WiFi</a> | <a href='/dmx'>DMX/Motion</a> | <a href='/hardware'>Hardware</a></p>";
  s += "</body></html>";
  server.send(200, "text/html", s);
}

static void handleWifi() {
  String s = htmlHead("WiFi");
  s += "<h2>WiFi Settings</h2>";
  s += "<form method='POST' action='/savewifi'>";
  s += "SSID: <input name='ssid' value='" + cfg.ssid + "'><br><br>";
  s += "Password: <input name='pass' type='password' value='" + cfg.pass + "'><br><br>";
  s += "<label><input name='st' type='checkbox' " + String(cfg.useStatic ? "checked" : "") + "> Static IP</label><br><br>";
  s += "IP: <input name='ip' value='" + cfg.ip.toString() + "'><br>";
  s += "GW: <input name='gw' value='" + cfg.gw.toString() + "'><br>";
  s += "Mask: <input name='mask' value='" + cfg.mask.toString() + "'><br><br>";
  s += "<button type='submit'>Save & Reboot</button>";
  s += "</form>";
  s += "<p><a href='/'>Back</a></p></body></html>";
  server.send(200, "text/html", s);
}

static void handleHardware() {
  String s = htmlHead("Hardware");
  s += "<h2>Hardware Mapping</h2>";
  s += "<form method='POST' action='/savehardware'>";

  s += "<h3>DC Motor (L298P)</h3>";
  s += "DIR pin: <input name='dc_dir' type='number' min='0' max='39' value='" + String(cfg.hardware.dcMotor.dirPin) + "'><br><br>";
  s += "PWM pin: <input name='dc_pwm' type='number' min='0' max='39' value='" + String(cfg.hardware.dcMotor.pwmPin) + "'><br><br>";
  s += "PWM channel: <input name='dc_ch' type='number' min='0' max='15' value='" + String(cfg.hardware.dcMotor.pwmChannel) + "'><br><br>";
  s += "PWM Hz: <input name='dc_hz' type='number' min='1' max='20000' value='" + String(cfg.hardware.dcMotor.pwmHz) + "'><br><br>";
  s += "PWM bits: <input name='dc_bits' type='number' min='1' max='16' value='" + String(cfg.hardware.dcMotor.pwmBits) + "'><br><br>";

  s += "<h3>Stepper (ULN2003)</h3>";
  s += "IN1: <input name='st_in1' type='number' min='0' max='39' value='" + String(cfg.hardware.stepper.in1) + "'><br><br>";
  s += "IN2: <input name='st_in2' type='number' min='0' max='39' value='" + String(cfg.hardware.stepper.in2) + "'><br><br>";
  s += "IN3: <input name='st_in3' type='number' min='0' max='39' value='" + String(cfg.hardware.stepper.in3) + "'><br><br>";
  s += "IN4: <input name='st_in4' type='number' min='0' max='39' value='" + String(cfg.hardware.stepper.in4) + "'><br><br>";

  s += "<h3>Relay</h3>";
  s += "Pin: <input name='rl_pin' type='number' min='0' max='39' value='" + String(cfg.hardware.relay.pin) + "'><br><br>";
  s += "<label><input name='rl_ah' type='checkbox' " + String(cfg.hardware.relay.activeHigh ? "checked" : "") + "> Active high</label><br><br>";

  s += "<h3>Status LED</h3>";
  s += "Pin: <input name='led_pin' type='number' min='0' max='39' value='" + String(cfg.hardware.led.pin) + "'><br><br>";
  s += "<label><input name='led_ah' type='checkbox' " + String(cfg.hardware.led.activeHigh ? "checked" : "") + "> Active high</label><br><br>";

  s += "<h3>Pixel Strip (NeoPixel)</h3>";
  s += "Pin: <input name='px_pin' type='number' min='0' max='39' value='" + String(cfg.hardware.pixels.pin) + "'><br><br>";
  s += "Count: <input name='px_count' type='number' min='0' max='1024' value='" + String(cfg.hardware.pixels.count) + "'><br><br>";
  s += "Brightness: <input name='px_brightness' type='number' min='0' max='255' value='" + String(cfg.hardware.pixels.brightness) + "'><br><br>";

  s += "<h3>Home Button</h3>";
  s += "Pin: <input name='home_pin' type='number' min='0' max='39' value='" + String(cfg.hardware.homeButtonPin) + "'><br><br>";

  s += "<button type='submit'>Save & Apply</button>";
  s += "</form>";

  s += "<h3>Quick Tests</h3>";
  s += "<p><a href='/test/relay/on'>Relay ON</a> | <a href='/test/relay/off'>Relay OFF</a></p>";
  s += "<p><a href='/test/led/on'>LED ON</a> | <a href='/test/led/off'>LED OFF</a></p>";
  s += "<p><a href='/test/pixels/white'>Pixels White</a> | <a href='/test/pixels/off'>Pixels Off</a></p>";

  s += "<p><a href='/'>Back</a></p></body></html>";
  server.send(200, "text/html", s);
}

static void handleDmx() {
  String s = htmlHead("DMX/Motion");
  s += "<h2>DMX / Motion</h2>";
  s += "<form method='POST' action='/savedmx'>";
  s += "Universe: <input name='u' type='number' min='1' max='63999' value='" + String(cfg.universe) + "'><br><br>";
  s += "Start Addr: <input name='a' type='number' min='1' max='508' value='" + String(cfg.startAddr) + "'>";
  s += " <small>(uses 4 slots)</small><br><br>";

  s += "sACN Mode: <select name='m'>";
  s += String("<option value='0'") + (cfg.sacnMode==SACN_UNICAST ? " selected" : "") + ">Unicast</option>";
  s += String("<option value='1'") + (cfg.sacnMode==SACN_MULTICAST ? " selected" : "") + ">Multicast</option>";
  s += "</select><br><br>";

  s += "<h3>DC Motor</h3>";
  s += "Deadband: <input name='db' type='number' min='0' max='20000' value='" + String(cfg.dcDeadband) + "'><br><br>";
  s += "Max PWM: <input name='mx' type='number' min='1' max='255' value='" + String(cfg.dcMaxPwm) + "'><br><br>";
  s += "Stop mode: <select name='sm'>";
  s += String("<option value='0'") + (cfg.dcStopMode==DC_COAST ? " selected" : "") + ">Coast</option>";
  s += String("<option value='1'") + (cfg.dcStopMode==DC_BRAKE ? " selected" : "") + ">Brake (treated as Coast)</option>";
  s += "</select><br><br>";

  s += "<h3>Stepper</h3>";
  s += "Steps/Rev: <input name='spr' type='number' min='200' max='20000' value='" + String(cfg.stepsPerRev) + "'><br><br>";
  s += "Max deg/sec: <input name='mdps' type='number' step='0.1' min='1' max='720' value='" + String(cfg.maxDegPerSec) + "'><br><br>";

  s += "<h3>Soft Limits</h3>";
  s += "<label><input name='lim' type='checkbox' " + String(cfg.limitsEnabled ? "checked" : "") + "> Enable limits</label><br><br>";
  s += "Min deg: <input name='min' type='number' step='0.1' value='" + String(cfg.minDeg) + "'><br><br>";
  s += "Max deg: <input name='max' type='number' step='0.1' value='" + String(cfg.maxDeg) + "'><br><br>";

  s += "<h3>DMX Loss</h3>";
  s += "Timeout (ms): <input name='to' type='number' min='100' max='60000' value='" + String(cfg.lossTimeoutMs) + "'><br><br>";
  s += "On loss: <select name='lm'>";
  s += String("<option value='0'") + (cfg.lossMode==LOSS_FORCE_OFF ? " selected" : "") + ">Force OFF</option>";
  s += String("<option value='1'") + (cfg.lossMode==LOSS_FORCE_ON  ? " selected" : "") + ">Force ON (treated as OFF)</option>";
  s += String("<option value='2'") + (cfg.lossMode==LOSS_HOLD_LAST ? " selected" : "") + ">Hold Last</option>";
  s += "</select><br><br>";

  s += "<button type='submit'>Save</button>";
  s += "</form>";
  s += "<p><a href='/'>Back</a></p></body></html>";
  server.send(200, "text/html", s);
}

static void handleSaveWifi() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
  cfg.ssid = server.arg("ssid");
  cfg.pass = server.arg("pass");
  cfg.useStatic = server.hasArg("st");

  IPAddress ip,gw,mask;
  if (parseIp(server.arg("ip"), ip)) cfg.ip = ip;
  if (parseIp(server.arg("gw"), gw)) cfg.gw = gw;
  if (parseIp(server.arg("mask"), mask)) cfg.mask = mask;

  saveConfig();
  server.send(200, "text/plain", "Saved. Rebooting...");
  delay(400);
  ESP.restart();
}

static void handleSaveDmx() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }

  cfg.universe = (uint16_t)server.arg("u").toInt();
  cfg.startAddr = (uint16_t)server.arg("a").toInt();
  cfg.sacnMode = (SacnMode)server.arg("m").toInt();

  cfg.dcDeadband = (int16_t)server.arg("db").toInt();
  cfg.dcMaxPwm = (uint8_t)server.arg("mx").toInt();
  cfg.dcStopMode = (DcStopMode)server.arg("sm").toInt();

  cfg.stepsPerRev = (uint16_t)server.arg("spr").toInt();
  cfg.maxDegPerSec = server.arg("mdps").toFloat();

  cfg.limitsEnabled = server.hasArg("lim");
  cfg.minDeg = server.arg("min").toFloat();
  cfg.maxDeg = server.arg("max").toFloat();

  cfg.lossTimeoutMs = (uint32_t)server.arg("to").toInt();
  cfg.lossMode = (DmxLossMode)server.arg("lm").toInt();

  sanity();
  saveConfig();

  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleSaveHardware() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }

  cfg.hardware.dcMotor.dirPin = (uint8_t)server.arg("dc_dir").toInt();
  cfg.hardware.dcMotor.pwmPin = (uint8_t)server.arg("dc_pwm").toInt();
  cfg.hardware.dcMotor.pwmChannel = (uint8_t)server.arg("dc_ch").toInt();
  cfg.hardware.dcMotor.pwmHz = (uint32_t)server.arg("dc_hz").toInt();
  cfg.hardware.dcMotor.pwmBits = (uint8_t)server.arg("dc_bits").toInt();

  cfg.hardware.stepper.in1 = (uint8_t)server.arg("st_in1").toInt();
  cfg.hardware.stepper.in2 = (uint8_t)server.arg("st_in2").toInt();
  cfg.hardware.stepper.in3 = (uint8_t)server.arg("st_in3").toInt();
  cfg.hardware.stepper.in4 = (uint8_t)server.arg("st_in4").toInt();

  cfg.hardware.relay.pin = (uint8_t)server.arg("rl_pin").toInt();
  cfg.hardware.relay.activeHigh = server.hasArg("rl_ah");

  cfg.hardware.led.pin = (uint8_t)server.arg("led_pin").toInt();
  cfg.hardware.led.activeHigh = server.hasArg("led_ah");

  cfg.hardware.pixels.pin = (uint8_t)server.arg("px_pin").toInt();
  cfg.hardware.pixels.count = (uint16_t)server.arg("px_count").toInt();
  cfg.hardware.pixels.brightness = (uint8_t)server.arg("px_brightness").toInt();

  cfg.hardware.homeButtonPin = (uint8_t)server.arg("home_pin").toInt();

  saveConfig();
  applyHardwareConfig();
  pinMode(cfg.hardware.homeButtonPin, INPUT_PULLUP);

  server.sendHeader("Location", "/hardware");
  server.send(303);
}

static void handleHome() {
  // set current step position as 0 degrees
  cfg.homeOffsetSteps = stepperCurrentPosition();
  saveConfig();
  server.sendHeader("Location", "/");
  server.send(303);
}

void setupWeb() {
  server.on("/", handleRoot);
  server.on("/wifi", handleWifi);
  server.on("/dmx", handleDmx);
  server.on("/hardware", handleHardware);
  server.on("/savewifi", handleSaveWifi);
  server.on("/savedmx", handleSaveDmx);
  server.on("/savehardware", handleSaveHardware);
  server.on("/home", handleHome);

  // Quick test endpoints
  server.on("/test/dc/fwd", [](){
    dcApplySigned(+20000);
    server.sendHeader("Location","/");
    server.send(303);
  });
  server.on("/test/dc/rev", [](){
    dcApplySigned(-20000);
    server.sendHeader("Location","/");
    server.send(303);
  });
  server.on("/test/dc/stop", [](){
    dcStop();
    server.sendHeader("Location","/");
    server.send(303);
  });

  server.on("/test/step/0",   [](){ stepperSetTargetDeg(0);   server.sendHeader("Location","/"); server.send(303); });
  server.on("/test/step/90",  [](){ stepperSetTargetDeg(90);  server.sendHeader("Location","/"); server.send(303); });
  server.on("/test/step/180", [](){ stepperSetTargetDeg(180); server.sendHeader("Location","/"); server.send(303); });
  server.on("/test/step/270", [](){ stepperSetTargetDeg(270); server.sendHeader("Location","/"); server.send(303); });
  server.on("/test/step/360", [](){ stepperSetTargetDeg(360); server.sendHeader("Location","/"); server.send(303); });

  server.on("/test/relay/on", [](){ setRelay(true); server.sendHeader("Location","/hardware"); server.send(303); });
  server.on("/test/relay/off", [](){ setRelay(false); server.sendHeader("Location","/hardware"); server.send(303); });
  server.on("/test/led/on", [](){ setStatusLed(true); server.sendHeader("Location","/hardware"); server.send(303); });
  server.on("/test/led/off", [](){ setStatusLed(false); server.sendHeader("Location","/hardware"); server.send(303); });
  server.on("/test/pixels/white", [](){ setAllPixels(255, 255, 255); showPixels(); server.sendHeader("Location","/hardware"); server.send(303); });
  server.on("/test/pixels/off", [](){ setAllPixels(0, 0, 0); showPixels(); server.sendHeader("Location","/hardware"); server.send(303); });

  server.begin();
}

void handleWeb() {
  server.handleClient();
}
