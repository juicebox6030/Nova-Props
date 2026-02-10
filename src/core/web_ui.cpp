#include "core/web_ui.h"

#include "core/features.h"

#if USE_WEB_UI

#include "platform/compat/http_server.h"

#include "core/config.h"
#include "core/subdevices.h"
#include "platform/platform_services.h"
#include "platform/config_storage.h"
#include "platform/dmx_sacn.h"

static HttpServer server(80);

static String esc(const String& in) {
  String o = in;
  o.replace("&", "&amp;");
  o.replace("<", "&lt;");
  o.replace(">", "&gt;");
  o.replace("\"", "&quot;");
  return o;
}

static String htmlHead(const String& title) {
  return String("<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>") + title + "</title></head>"
                "<body style='font-family:sans-serif;max-width:980px;margin:16px;'>";
}

static String typeOptions(SubdeviceType selected) {
  String s;
  for (uint8_t t = SUBDEVICE_STEPPER; t <= SUBDEVICE_PIXELS; t++) {
    s += "<option value='" + String(t) + "'";
    if (t == selected) s += " selected";
    s += ">" + subdeviceTypeName((SubdeviceType)t) + "</option>";
  }
  return s;
}

static void renderTypeSpecificFields(String& s, const SubdeviceConfig& sd) {
  switch (sd.type) {
    case SUBDEVICE_STEPPER:
      s += "<fieldset><legend>Stepper</legend>IN1 <input name='st1' type='number' value='" + String(sd.stepper.in1) + "'> "
           "IN2 <input name='st2' type='number' value='" + String(sd.stepper.in2) + "'> "
           "IN3 <input name='st3' type='number' value='" + String(sd.stepper.in3) + "'> "
           "IN4 <input name='st4' type='number' value='" + String(sd.stepper.in4) + "'><br><br>"
           "Steps/rev <input name='stspr' type='number' value='" + String(sd.stepper.stepsPerRev) + "'> "
           "Max deg/sec <input name='stspd' type='number' step='0.1' value='" + String(sd.stepper.maxDegPerSec) + "'><br><br>"
           "<label><input type='checkbox' name='stlim' " + String(sd.stepper.limitsEnabled ? "checked" : "") + ">Limits</label> "
           "Min <input name='stmin' type='number' step='0.1' value='" + String(sd.stepper.minDeg) + "'> "
           "Max <input name='stmax' type='number' step='0.1' value='" + String(sd.stepper.maxDeg) + "'><br><br>"
           "Home offset (steps) <input name='sthomeofs' type='number' value='" + String(sd.stepper.homeOffsetSteps) + "'><br><br>"
           "<label><input type='checkbox' name='sthomeen' " + String(sd.stepper.homeSwitchEnabled ? "checked" : "") + ">Home/zero switch enabled</label> "
           "Pin <input name='sthomepin' type='number' value='" + String(sd.stepper.homeSwitchPin == 255 ? -1 : sd.stepper.homeSwitchPin) + "'> "
           "<label><input type='checkbox' name='sthomeal' " + String(sd.stepper.homeSwitchActiveLow ? "checked" : "") + ">Active low</label></fieldset><br>";
      break;
    case SUBDEVICE_DC_MOTOR:
      s += "<fieldset><legend>DC Motor</legend>DIR <input name='dcdir' type='number' value='" + String(sd.dc.dirPin) + "'> "
           "PWM <input name='dcpwm' type='number' value='" + String(sd.dc.pwmPin) + "'> "
           "CH <input name='dcch' type='number' value='" + String(sd.dc.pwmChannel) + "'><br><br>"
           "Hz <input name='dchz' type='number' value='" + String(sd.dc.pwmHz) + "'> "
           "Bits <input name='dcbits' type='number' value='" + String(sd.dc.pwmBits) + "'> "
           "Deadband <input name='dcdb' type='number' value='" + String(sd.dc.deadband) + "'> "
           "MaxPWM <input name='dcmx' type='number' value='" + String(sd.dc.maxPwm) + "'></fieldset><br>";
      break;
    case SUBDEVICE_RELAY:
      s += "<fieldset><legend>Relay</legend>Relay pin <input name='rlpin' type='number' value='" + String(sd.relay.pin) + "'> "
           "Relay active high <input type='checkbox' name='rlah' " + String(sd.relay.activeHigh ? "checked" : "") + "></fieldset><br>";
      break;
    case SUBDEVICE_LED:
      s += "<fieldset><legend>LED</legend>LED pin <input name='ledpin' type='number' value='" + String(sd.led.pin) + "'> "
           "LED active high <input type='checkbox' name='ledah' " + String(sd.led.activeHigh ? "checked" : "") + "></fieldset><br>";
      break;
    case SUBDEVICE_PIXELS:
      s += "<fieldset><legend>Pixel Strip</legend>Pixel pin <input name='pxpin' type='number' value='" + String(sd.pixels.pin) + "'> "
           "Count <input name='pxcount' type='number' value='" + String(sd.pixels.count) + "'> "
           "Brightness <input name='pxb' type='number' value='" + String(sd.pixels.brightness) + "'></fieldset><br>";
      break;
    default:
      break;
  }
}

static void handleRoot() {
  String s = htmlHead(platformDeviceName());
  s += "<h2>" + platformDeviceName() + "</h2>";
  s += "<p><b>Mode:</b> ";
  if (platformIsStaMode()) s += "STA";
  if (platformIsApMode()) s += (platformIsStaMode() ? " + AP" : "AP");
  if (platformIsStaMode()) s += " | <b>STA IP:</b> " + platformStaIp();
  if (platformIsApMode()) s += " | <b>AP IP:</b> " + platformApIp();
  s += "</p>";
  s += "<p><b>Packets:</b> " + String(sacnPacketCounter()) + " | <b>Last Universe:</b> " + String(lastUniverseSeen()) + " | <b>DMX Active:</b> " + String(dmxActive() ? "yes" : "no") + "</p>";
  s += "<p><a href='/wifi'>WiFi</a> | <a href='/dmx'>sACN</a> | <a href='/subdevices'>Subdevices</a></p>";

  s += "<h3>Configured Subdevices (" + String(cfg.subdeviceCount) + "/" + String(MAX_SUBDEVICES) + ")</h3><ul>";
  for (uint8_t i = 0; i < cfg.subdeviceCount; i++) {
    auto& sd = cfg.subdevices[i];
    s += "<li>#" + String(i + 1) + " <b>" + esc(String(sd.name)) + "</b> [" + subdeviceTypeName(sd.type) + "] U" + String(sd.map.universe) + " @ " + String(sd.map.startAddr);
    s += sd.enabled ? " (enabled)" : " (disabled)";
    s += "</li>";
  }
  s += "</ul></body></html>";
  server.send(200, "text/html", s);
}

static void handleWifi() {
  String s = htmlHead("WiFi");
  s += "<h2>WiFi Settings</h2><form method='POST' action='/savewifi'>";
  s += "SSID: <input name='ssid' value='" + esc(cfg.ssid) + "'><br><br>";
  s += "Password: <input name='pass' type='password' value='" + esc(cfg.pass) + "'><br><br>";
  s += "<label><input name='st' type='checkbox' " + String(cfg.useStatic ? "checked" : "") + "> Static IP</label><br><br>";
  s += "IP: <input name='ip' value='" + cfg.ip.toString() + "'><br>";
  s += "GW: <input name='gw' value='" + cfg.gw.toString() + "'><br>";
  s += "Mask: <input name='mask' value='" + cfg.mask.toString() + "'><br><br>";
  s += "<button type='submit'>Save & Reboot</button></form><p><a href='/'>Back</a></p></body></html>";
  server.send(200, "text/html", s);
}

static void handleDmx() {
  String s = htmlHead("sACN");
  s += "<h2>sACN Settings</h2><form method='POST' action='/savedmx'>";
  s += "Mode: <select name='m'>";
  s += String("<option value='0'") + (cfg.sacnMode == SACN_UNICAST ? " selected" : "") + ">Unicast</option>";
  s += String("<option value='1'") + (cfg.sacnMode == SACN_MULTICAST ? " selected" : "") + ">Multicast</option>";
  s += "</select><br><br>";
  s += "DMX loss timeout (ms): <input name='to' type='number' min='100' max='60000' value='" + String(cfg.lossTimeoutMs) + "'><br><br>";
  s += "On loss: <select name='lm'>";
  s += String("<option value='0'") + (cfg.lossMode == LOSS_FORCE_OFF ? " selected" : "") + ">Force OFF</option>";
  s += String("<option value='2'") + (cfg.lossMode == LOSS_HOLD_LAST ? " selected" : "") + ">Hold Last</option>";
  s += "</select><br><br>";
  s += "<button type='submit'>Save</button></form><p><a href='/'>Back</a></p></body></html>";
  server.send(200, "text/html", s);
}

static void renderSubdeviceForm(String& s, uint8_t i) {
  auto& sd = cfg.subdevices[i];
  s += "<details style='border:1px solid #ccc;padding:8px;margin:10px 0;' open><summary><b>#" + String(i + 1) + " " + esc(String(sd.name)) + "</b> (" + subdeviceTypeName(sd.type) + ")</summary>";
  s += "<form method='POST' action='/subdevices/update'>";
  s += "<input type='hidden' name='id' value='" + String(i) + "'>";
  s += "Name: <input name='name' value='" + esc(String(sd.name)) + "'> &nbsp;";
  s += "Enabled: <input type='checkbox' name='en' " + String(sd.enabled ? "checked" : "") + "><br><br>";
  s += "Type: <select name='type'>" + typeOptions(sd.type) + "</select><br><br>";
  s += "Universe: <input name='u' type='number' min='1' max='63999' value='" + String(sd.map.universe) + "'> &nbsp;";
  s += "Start addr: <input name='a' type='number' min='1' max='512' value='" + String(sd.map.startAddr) + "'><br><br>";

  renderTypeSpecificFields(s, sd);

  s += "<button type='submit'>Save Subdevice</button> ";
  s += "<a href='/subdevices/test?id=" + String(i) + "'>Run Test</a> | ";
  if (sd.type == SUBDEVICE_STEPPER) {
    s += "<a href='/subdevices/homezero?id=" + String(i) + "'>Home/Zero</a> | ";
  }
  s += "<a href='/subdevices/delete?id=" + String(i) + "' onclick=\"return confirm('Delete subdevice?');\">Delete</a>";
  s += "</form></details>";
}

static void handleSubdevices() {
  String s = htmlHead("Subdevices");
  s += "<h2>Subdevices</h2>";
  s += "<p>Add hardware blocks and map each to Universe/Address for sACN.</p>";

  s += "<form method='POST' action='/subdevices/add' style='padding:8px;border:1px solid #ccc;'>";
  s += "Name <input name='name' placeholder='optional'> ";
  s += "Type <select name='type'>" + typeOptions(SUBDEVICE_STEPPER) + "</select> ";
  s += "<button type='submit'>Add Subdevice</button></form>";

  for (uint8_t i = 0; i < cfg.subdeviceCount; i++) renderSubdeviceForm(s, i);

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
  cfg.sacnMode = (SacnMode)server.arg("m").toInt();
  cfg.lossTimeoutMs = (uint32_t)server.arg("to").toInt();
  cfg.lossMode = (DmxLossMode)server.arg("lm").toInt();
  sanity();
  saveConfig();
  restartSacn();
  server.sendHeader("Location", "/dmx");
  server.send(303);
}

static void handleAddSubdevice() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
  SubdeviceType type = (SubdeviceType)server.arg("type").toInt();
  if (!addSubdevice(type, server.arg("name"))) {
    server.send(400, "text/plain", "Cannot add subdevice (max reached)");
    return;
  }
  saveConfig();
  initSubdevices();
  restartSacn();
  server.sendHeader("Location", "/subdevices");
  server.send(303);
}

static void handleUpdateSubdevice() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
  int idx = server.arg("id").toInt();
  if (idx < 0 || idx >= cfg.subdeviceCount) { server.send(400, "text/plain", "Invalid id"); return; }
  SubdeviceConfig& sd = cfg.subdevices[idx];

  sd.enabled = server.hasArg("en");
  sd.type = (SubdeviceType)server.arg("type").toInt();
  String name = server.arg("name");
  if (name.length() == 0) name = String("subdevice-") + String(idx + 1);
  name.toCharArray(sd.name, sizeof(sd.name));

  sd.map.universe = (uint16_t)server.arg("u").toInt();
  sd.map.startAddr = (uint16_t)server.arg("a").toInt();

  if (sd.type == SUBDEVICE_STEPPER) {
    sd.stepper.in1 = (uint8_t)server.arg("st1").toInt();
    sd.stepper.in2 = (uint8_t)server.arg("st2").toInt();
    sd.stepper.in3 = (uint8_t)server.arg("st3").toInt();
    sd.stepper.in4 = (uint8_t)server.arg("st4").toInt();
    sd.stepper.stepsPerRev = (uint16_t)server.arg("stspr").toInt();
    sd.stepper.maxDegPerSec = server.arg("stspd").toFloat();
    sd.stepper.limitsEnabled = server.hasArg("stlim");
    sd.stepper.minDeg = server.arg("stmin").toFloat();
    sd.stepper.maxDeg = server.arg("stmax").toFloat();
    sd.stepper.homeOffsetSteps = server.arg("sthomeofs").toInt();
    sd.stepper.homeSwitchEnabled = server.hasArg("sthomeen");
    int homePin = server.arg("sthomepin").toInt();
    sd.stepper.homeSwitchPin = (homePin < 0) ? 255 : (uint8_t)homePin;
    sd.stepper.homeSwitchActiveLow = server.hasArg("sthomeal");
  } else if (sd.type == SUBDEVICE_DC_MOTOR) {
    sd.dc.dirPin = (uint8_t)server.arg("dcdir").toInt();
    sd.dc.pwmPin = (uint8_t)server.arg("dcpwm").toInt();
    sd.dc.pwmChannel = (uint8_t)server.arg("dcch").toInt();
    sd.dc.pwmHz = (uint32_t)server.arg("dchz").toInt();
    sd.dc.pwmBits = (uint8_t)server.arg("dcbits").toInt();
    sd.dc.deadband = (int16_t)server.arg("dcdb").toInt();
    sd.dc.maxPwm = (uint8_t)server.arg("dcmx").toInt();
  } else if (sd.type == SUBDEVICE_RELAY) {
    sd.relay.pin = (uint8_t)server.arg("rlpin").toInt();
    sd.relay.activeHigh = server.hasArg("rlah");
  } else if (sd.type == SUBDEVICE_LED) {
    sd.led.pin = (uint8_t)server.arg("ledpin").toInt();
    sd.led.activeHigh = server.hasArg("ledah");
  } else if (sd.type == SUBDEVICE_PIXELS) {
    sd.pixels.pin = (uint8_t)server.arg("pxpin").toInt();
    sd.pixels.count = (uint16_t)server.arg("pxcount").toInt();
    sd.pixels.brightness = (uint8_t)server.arg("pxb").toInt();
  }

  sanity();
  saveConfig();
  initSubdevices();
  restartSacn();

  server.sendHeader("Location", "/subdevices");
  server.send(303);
}

static void handleDeleteSubdevice() {
  int idx = server.arg("id").toInt();
  if (!deleteSubdevice((uint8_t)idx)) {
    server.send(400, "text/plain", "Invalid id");
    return;
  }
  saveConfig();
  initSubdevices();
  restartSacn();
  server.sendHeader("Location", "/subdevices");
  server.send(303);
}

static void handleTestSubdevice() {
  int idx = server.arg("id").toInt();
  if (!runSubdeviceTest((uint8_t)idx)) {
    server.send(400, "text/plain", "Invalid id or unsupported type");
    return;
  }
  server.sendHeader("Location", "/subdevices");
  server.send(303);
}

static void handleHomeZeroSubdevice() {
  int idx = server.arg("id").toInt();
  if (!homeZeroSubdevice((uint8_t)idx)) {
    server.send(400, "text/plain", "Invalid id or non-stepper subdevice");
    return;
  }
  server.sendHeader("Location", "/subdevices");
  server.send(303);
}

void setupWeb() {
  server.on("/", handleRoot);
  server.on("/wifi", handleWifi);
  server.on("/dmx", handleDmx);
  server.on("/subdevices", handleSubdevices);

  server.on("/savewifi", handleSaveWifi);
  server.on("/savedmx", handleSaveDmx);
  server.on("/subdevices/add", handleAddSubdevice);
  server.on("/subdevices/update", handleUpdateSubdevice);
  server.on("/subdevices/delete", handleDeleteSubdevice);
  server.on("/subdevices/test", handleTestSubdevice);
  server.on("/subdevices/homezero", handleHomeZeroSubdevice);

  server.begin();
}

void handleWeb() {
  server.handleClient();
}

#else

void setupWeb() {}
void handleWeb() {}

#endif
