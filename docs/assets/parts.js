// The parts of a clock (HARDWARE.md section 7, guides/building.md sections 1 and 2, hardware/enclosure/README.md
// section 2), the prototype's wiring (guides/building.md section 2), as data for the build page: the parts table, its
// CSV download, and the wiring diagram. Keep it in step with those files.

const lcsc = c => `https://www.lcsc.com/product-detail/${c}.html`;
const ali = q => `https://www.aliexpress.com/w/wholesale-${q.trim().replace(/\s+/g, '-')}.html`;
const amazon = q => `https://www.amazon.com/s?k=${encodeURIComponent(q)}`;

// [ref, qty, part, details, LCSC, side, link, link label]
export const PARTS = {
  board: [
    ['U1', '1', 'ESP32-S3-WROOM-1-N16R8', '16 MB flash and 8 MB PSRAM. Use exactly this variant: the others lack the memory for the stored sounds. $5.19 at LCSC (October 2026)', 'C2913202', 'back'],
    ['U4', '1', 'MAX98357AETE+T, TQFN-16', 'The I2S amplifier, up to 3 W into the speaker. $1.33 at LCSC (October 2026)', 'C910544', 'back'],
    ['U3', '1', 'AMS1117-3.3, SOT-223', 'Makes 3.3 V for the module and the display', 'C6186', 'back'],
    ['U2', '1', '5-way switch, 10 x 10 mm, with a centre push', 'Alps SKQUCAA010 or a copy, sold as "5-way switch 10*10*10 mm". Not SKQUAAA010, which has no centre push', '', 'front', ali('5 way switch 10x10x10mm'), 'Search'],
    ['J1', '1', 'USB-C receptacle, 6-pin, power only (GCT USB4125-GF-A)', 'On the front or the back: its pads are on both sides', 'C3151650', 'front or back'],
    ['J3, J5', '2', '2-pin screw terminal, 5.0 mm (MaiXu MX126-5.0-02P)', 'The speaker (J3) and the relay output (J5)', 'C5188434', 'front'],
    ['C1', '1', '470 µF 10 V low-ESR electrolytic, 8 x 10.5 mm', 'Panasonic EEE-FK1A471P. At most 10.5 mm tall, for the case', 'C178530', 'front'],
    ['C2, C3, C6', '3', '22 µF 25 V ceramic, 1206, X5R or X7R', 'Samsung CL31A226KAHNNNE', 'C12891', 'back'],
    ['C4, C7', '2', '100 nF 50 V X7R, 1206', 'Samsung CL31B104KBCNNNC', 'C24497', 'back'],
    ['C5', '1', '1 µF 50 V X7R, 1206', 'Samsung CL31B105KBHNNNE', 'C1848', 'back'],
    ['R1-R5, R8, R9', '7', '4.7 kΩ 1 %, 1206', 'UNI-ROYAL 1206W4F4701T5E', 'C17936', 'R1, R2 front; the rest back'],
    ['R10', '1', '100 Ω 1 %, 1206', 'The LED\'s resistor: 300 to 330 Ω with a red LED', 'C17901', 'back'],
    ['D1', '1', 'Blue LED, 1206', 'NationStar NCD1206B1', 'C130717', 'front'],
    ['DS1 or DS2', '1', 'Male pin header, 2.54 mm: 4 pins (OLED) or 8 pins (TFT)', 'Pushed in from the back, its pins standing up on the front; the display plugs onto it', '', 'front', ali('2.54mm male pin header'), 'Search'],
    ['J6', '0 or 1', '1x6 pin header, 2.54 mm, and a jumper cap', 'Optional: none with the colour screen (it sits under it); flash with the press-in programmer instead', '', 'front', ali('2.54mm male pin header jumper cap'), 'Search'],
    ['R7', '0', '100 kΩ 1 %, 1206', 'Only for 15 dB amplifier gain', 'C17900', 'back'],
    ['R11', '0', '100 Ω 1 %, 1206', 'Only for a resistor in series with the relay output', 'C17901', 'back'],
    ['J2', '0', 'Barrel jack, CUI PJ-102AH', 'A fallback for a broken USB-C on a board used without the case: the case has no opening for it', 'C3096093', 'front'],
    ['PCB', '1', 'The circuit board, 59.6 x 50 mm', 'Two layers, 1.6 mm, lead-free HASL or ENIG. Made from the KiCad project (see The board)', '', '', '#board', 'How to order'],
  ],
  extras: [
    ['', '1', 'Display: 1.54" ST7789 240 x 240 IPS colour screen, 8 pins', 'An IPS panel reads well from an angle. Or a 1.3" SH1106 (or 0.96" SSD1306) 128 x 64 I2C OLED, 4 pins', '', '', 'https://www.amazon.com/dp/B0DN9NMBFW', 'The module this case fits'],
    ['', '1', 'Female pin header, 2.54 mm, 8.5 mm tall: 8 pins (TFT) or 4 (OLED)', 'Soldered onto the display\'s back', '', '', ali('2.54mm female header 8.5mm'), 'Search'],
    ['', '1', 'Speaker, 40 mm round, 4 Ω 3 W', 'Up to 18.5 mm deep with the colour screen\'s case (16.5 mm with the OLED\'s); a rim about 2 mm thick', '', '', ali('40mm speaker 4ohm 3w'), 'Search'],
    ['', '1', 'USB-C charger, 5 V 2 A', 'Never more than 5 V', '', '', '', ''],
    ['', '1', 'USB-serial adapter, 3.3 V (CP2102, or a CH340 set to 3.3 V)', 'For the first flash through J6', '', '', amazon('CP2102 USB to TTL 3.3V'), 'Search'],
    ['', '3', 'The case: front, back and knob, printed', 'PLA or PETG, no supports (see The case)', '', '', '#case', 'What to print'],
    ['', '0 or 6', 'M3 x 16 countersunk screws', 'Optional, for extra hold; they cut their own thread', '', '', ali('M3x16 countersunk screw'), 'Search'],
    ['', '0 or 1', 'Light pipe: 1.75 mm clear filament', 'Optional: 13.8 mm of it (11.8 mm with the OLED) carries the LED\'s light to the face', '', '', '', ''],
    ['', '0 or 1', 'Relay module that switches on a 3.3 V input', 'Optional: the relay output is a 3.3 V signal, for a room light before Fajr', '', '', ali('3.3V relay module'), 'Search'],
    ['', '0 or 1', 'The press-in programmer', 'Optional: flashes through J6 without soldering a header there', '', '', '#flash', 'See Flashing'],
  ],
  prototype: [
    ['', '1', 'ESP32-S3-DevKitC-1 N16R8', 'The same module on a development board', '', '', 'https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/', 'Espressif'],
    ['', '1', 'MAX98357A breakout (Adafruit 3006, or a clone)', 'The amplifier, ready to wire', '', '', 'https://www.adafruit.com/product/3006', 'Adafruit'],
    ['', '1', 'Speaker, a display and the 5-way switch', 'As for the board (five push buttons work too)', '', '', '', ''],
    ['', '1', 'An LED and its resistor', '100 Ω for a blue, green or white LED, 330 Ω for red', '', '', '', ''],
    ['', '1', '4.7 kΩ resistor', 'For the amplifier\'s SD pin', '', '', '', ''],
    ['', '1', 'Breadboard and jumper wires', '', '', '', '', ''],
  ],
};
export function partLink(p) {
  const [, , , , c, , link, label] = p;
  if (link) return { href: link, label };
  if (c) return { href: lcsc(c), label: c };
  return null;
}
export function partsCSV() {
  const q = v => `"${String(v).replace(/"/g, '""')}"`;
  const rows = [['Group', 'Ref', 'Qty', 'Part', 'Details', 'LCSC', 'Side', 'Link']];
  for (const [g, list] of [['Board', PARTS.board], ['Also needed', PARTS.extras], ['Prototype', PARTS.prototype]]) {
    for (const p of list) {
      const l = partLink(p);
      rows.push([g, p[0], p[1], p[2], p[3], p[4], p[5], l && !l.href.startsWith('#') ? l.href : '']);
    }
  }
  return '﻿' + rows.map(r => r.map(q).join(',')).join('\r\n') + '\r\n';
}

// ---------------------------------------------------------------- the prototype's wiring
// [DevKitC pin, part, part's pin, group]; GND is one pin with a wire to every part
export const GROUPS = {
  power: ['Power', '#b23b3b', 'The DevKitC\'s 5V pin carries the USB supply to the amplifier'],
  amp: ['Amplifier', '#2a78d6', 'I2S audio, and SD through 4.7 kΩ: high plays the left channel, low switches it off. GAIN stays open (9 dB)'],
  keys: ['5-way switch', '#1b9e6f', 'The firmware turns on the pull-ups; COM goes to GND'],
  oled: ['OLED (I2C)', '#7a55d6', 'Most OLED modules carry their own pull-ups'],
  tft: ['Colour screen (SPI)', '#7a55d6', 'The modules label their SPI clock and data SCL and SDA'],
  led: ['LED and relay', '#c98500', 'GPIO13: the status LED through its resistor. GPIO12: the relay module\'s input'],
};
export function wiring(display) {
  const W = [
    ['5V', 'Amplifier', 'Vin', 'power'], ['GND', 'Amplifier', 'GND', 'power'],
    ['GPIO5', 'Amplifier', 'BCLK', 'amp'], ['GPIO6', 'Amplifier', 'LRC', 'amp'], ['GPIO7', 'Amplifier', 'DIN', 'amp'],
    ['GPIO15', 'Amplifier', 'SD (4.7 kΩ)', 'amp'],
    ['GPIO14', '5-way switch', 'Up', 'keys'], ['GPIO10', '5-way switch', 'Down', 'keys'], ['GPIO21', '5-way switch', 'Left', 'keys'],
    ['GPIO47', '5-way switch', 'Right', 'keys'], ['GPIO11', '5-way switch', 'Select', 'keys'], ['GND', '5-way switch', 'COM', 'keys'],
  ];
  if (display === 'oled') {
    W.push(['3V3', 'OLED', 'VCC', 'oled'], ['GND', 'OLED', 'GND', 'oled'], ['GPIO8', 'OLED', 'SDA', 'oled'], ['GPIO9', 'OLED', 'SCL', 'oled']);
  } else {
    W.push(['3V3', 'Colour screen', 'VCC', 'tft'], ['GND', 'Colour screen', 'GND', 'tft'], ['GPIO9', 'Colour screen', 'SCL', 'tft'],
      ['GPIO8', 'Colour screen', 'SDA', 'tft'], ['GPIO16', 'Colour screen', 'RES', 'tft'], ['GPIO17', 'Colour screen', 'DC', 'tft'],
      ['GPIO18', 'Colour screen', 'CS', 'tft'], ['GPIO4', 'Colour screen', 'BLK', 'tft']);
  }
  W.push(['GPIO13', 'LED', '+ (resistor)', 'led'], ['GND', 'LED', '−', 'led'], ['GPIO12', 'Relay module', 'IN', 'led'], ['GND', 'Relay module', 'GND', 'led']);
  return W;
}
export const DEVKIT_ORDER = ['5V', '3V3', 'GND', 'GPIO5', 'GPIO6', 'GPIO7', 'GPIO15', 'GPIO14', 'GPIO10', 'GPIO21', 'GPIO47', 'GPIO11', 'GPIO8', 'GPIO9', 'GPIO16', 'GPIO17', 'GPIO18', 'GPIO4', 'GPIO13', 'GPIO12'];

// ---------------------------------------------------------------- the acceptance checklist (guides/building.md section 7)
export const CHECKS = [
  ['Size', 'The firmware fits its app slots (about 1.8 MB of 0x360000 bytes).'],
  ['Wi-Fi setup, both ways', 'Bluetooth from Chrome on Android and from the Home Assistant app (Select confirms, Bluetooth turns off after joining); the hotspot from an iPhone. A clock whose network went away reaches setup and reconnects when it returns.'],
  ['Downloads', 'Each sound downloads from the list. A sound stays as it was after pulling the network halfway, a file too big, too long, or not an MP3.'],
  ['Uploads', 'From the sounds page, with the same four failures.'],
  ['A power cut while a sound is saved', 'That sound counts as empty, its athan time is marked with three tones, and its default downloads again once online.'],
  ['Playback', 'Athan, Fajr athan, tawashih and tick play from memory at every volume from 10 to 100 %, with no hiss while idle. Pre-Fajr starts 25 minutes before Fajr with the relay on; the relay goes off 15 minutes after.'],
  ['Radio', 'A station plays for an hour without dropouts; a changed link in stations.json is picked up within 6 hours; an own link plays; after a router restart the stream comes back.'],
  ['Radio and the other sounds', 'An athan pauses the radio and the station comes back after it; stopping the athan leaves the radio off; a tick plays over the lowered radio.'],
  ['Controls', 'Web page, Home Assistant, screen and switch respond while a stream plays. Each direction does what its name says as you face the clock. Every menu row works; quick browsing never restarts it.'],
  ['Updates', 'An update from a GitHub Release installs. A version 2 clock does not see it.'],
  ['Power', 'No resets at full volume, no Wi-Fi buzz in the speaker, the gain set (HARDWARE.md section 4).'],
  ['Prayer times', 'Two days without the router miss no athan; another mosque downloads its year; December fetches the next year; estimated times work from 1 January; a broken file is refused; after a power cut without internet the clock recovers by itself.'],
  ['The colour screen', 'The Wi-Fi setup screen shows a teal icon and an orange "Press Select" on black; every icon shows; Screen Colour changes at once; unplugged and plugged back in, the screen comes back within 30 seconds.'],
  ['Screen settings', 'Brightness 10 % is readable in a dark room and 100 % in daylight; Screen at Night switches at Night From and back at Night Until; a dark screen lights for 2 minutes on a key press.'],
];
