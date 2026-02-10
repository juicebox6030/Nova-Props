#include "web_ui.h"

#include <WiFi.h>
#include <WebServer.h>

#include "app_config.h"
#include "dc_motor.h"
#include "dmx_sacn.h"
#include "stepper.h"
#include "wifi_ota.h"

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
  s += "<p><a href='/wifi'>WiFi</a> | <a href='/dmx'>DMX/Motion</a></p>";
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
  server.on("/savewifi", handleSaveWifi);
  server.on("/savedmx", handleSaveDmx);
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

  server.begin();
}

void handleWeb() {
  server.handleClient();
}
