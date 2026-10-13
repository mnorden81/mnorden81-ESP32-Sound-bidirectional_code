/*
 * test/test_crsf/test_main.cpp  –  ESP32-RC-Sound
 *
 * Unity-Tests fuer die gemeinsame CRSF-Bibliothek (lib/CRSF_ESP32), laufen
 * unter dem PlatformIO "native"-Environment ohne ESP32:
 *
 *   pio test -e native -f test_crsf
 *
 * Die CRSF-Klasse liest und schreibt ueber einen FakeStream statt ueber
 * einen UART. Geprueft wird gegen die CRSF-Spezifikation
 * (github.com/tbs-fpv/tbs-crsf-spec, crsf.md).
 */

#include <unity.h>
#include <vector>
#include <string>
#include "crsf_esp32.h"

// ── Hilfen ─────────────────────────────────────────────────────────────

// UART-Ersatz: rx wird von read_packets() gelesen, tx sammelt Gesendetes.
class FakeStream : public Stream {
public:
    std::vector<uint8_t> rx;
    std::vector<uint8_t> tx;
    size_t pos = 0;
    int available() override { return (int)(rx.size() - pos); }
    int read() override { return pos < rx.size() ? rx[pos++] : -1; }
    size_t write(const uint8_t* b, size_t n) override { tx.insert(tx.end(), b, b + n); return n; }
    void feed(const std::vector<uint8_t>& bytes) { rx.insert(rx.end(), bytes.begin(), bytes.end()); }
};

// Bitweise Referenz-CRC (MSB first), unabhaengig von den Tabellen im Code.
static uint8_t crc_ref(uint8_t poly, const uint8_t* p, size_t n) {
    uint8_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ poly) : (uint8_t)(crc << 1);
    }
    return crc;
}

// Kompletter Frame: Sync, Laenge, Typ, Payload, CRC (0xD5).
static std::vector<uint8_t> frame(uint8_t type, const std::vector<uint8_t>& payload, uint8_t sync = 0xC8) {
    std::vector<uint8_t> f{sync, (uint8_t)(payload.size() + 2), type};
    f.insert(f.end(), payload.begin(), payload.end());
    f.push_back(crc_ref(0xD5, &f[2], f.size() - 2));
    return f;
}

// RC-Kanal-Frame (0x16) mit 16 Kanaelen zu je 11 Bit, LSB first.
static std::vector<uint8_t> channels_frame(const uint16_t ch[16]) {
    std::vector<uint8_t> p(22, 0);
    for (int i = 0; i < 16; i++)
        for (int b = 0; b < 11; b++)
            if (ch[i] & (1u << b)) { int bit = i * 11 + b; p[bit / 8] |= (uint8_t)(1u << (bit % 8)); }
    return frame(CRSF_FRAMETYPE_RC_CHANNELS_PACKED, p);
}

// Prueft Laenge und Haupt-CRC eines gesendeten Frames ab Offset 0.
static void assert_valid_frame(const std::vector<uint8_t>& f) {
    TEST_ASSERT_TRUE(f.size() >= 4);
    TEST_ASSERT_EQUAL_UINT(f.size(), (size_t)f[1] + 2);
    TEST_ASSERT_EQUAL_HEX8(crc_ref(0xD5, &f[2], f[1] - 1), f.back());
}

static FakeStream io;
static CRSF crsf;

void setUp(void) {
    io = FakeStream();
    crsf = CRSF();
    crsf.attach_stream(&io);
}
void tearDown(void) {}

// ── CRC ────────────────────────────────────────────────────────────────

static void test_crc8_d5_matches_reference(void) {
    const uint8_t data[] = {0x16, 0x01, 0x02, 0xFF, 0x80, 0x7F};
    for (size_t n = 1; n <= sizeof(data); n++)
        TEST_ASSERT_EQUAL_HEX8(crc_ref(0xD5, data, n), crc8(data, (uint8_t)n));
}

static void test_crc8_ba_matches_spec_polynom(void) {
    // Vor v1.0.0 war die Tabelle falsch (00 BA B3 09 ...).
    for (int i = 0; i < 256; i++) {
        uint8_t b = (uint8_t)i;
        TEST_ASSERT_EQUAL_HEX8(crc_ref(0xBA, &b, 1), crc8_ba(&b, 1));
    }
}

// ── Parser ─────────────────────────────────────────────────────────────

static void test_parser_reads_whole_frame(void) {
    uint16_t ch[16];
    for (int i = 0; i < 16; i++) ch[i] = (uint16_t)(172 + i * 100);
    io.feed(channels_frame(ch));
    crsf.read_packets(0);
    TEST_ASSERT_EQUAL_UINT32(1, crsf.getValidFrames());
    TEST_ASSERT_EQUAL_UINT32(0, crsf.getCrcErrors());
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_UINT16(ch[i], crsf.get_crfs_channels(i));
}

static void test_parser_completes_frame_split_across_calls(void) {
    // Kernfall des Parser-Fixes: der Frame liegt beim ersten Aufruf nur
    // halb im UART-Puffer. Vorher ging er verloren und erzeugte CRC-Fehler.
    uint16_t ch[16];
    for (int i = 0; i < 16; i++) ch[i] = (uint16_t)(1811 - i * 50);
    std::vector<uint8_t> f = channels_frame(ch);
    for (size_t cut = 1; cut < f.size(); cut++) {
        setUp();
        io.feed(std::vector<uint8_t>(f.begin(), f.begin() + cut));
        crsf.read_packets(0);
        TEST_ASSERT_EQUAL_UINT32(0, crsf.getValidFrames());
        io.feed(std::vector<uint8_t>(f.begin() + cut, f.end()));
        crsf.read_packets(0);
        TEST_ASSERT_EQUAL_UINT32(1, crsf.getValidFrames());
        TEST_ASSERT_EQUAL_UINT32(0, crsf.getCrcErrors());
        TEST_ASSERT_EQUAL_UINT16(ch[15], crsf.get_crfs_channels(15));
    }
}

static void test_parser_byte_by_byte_several_frames(void) {
    uint16_t ch[16] = {0};
    std::vector<uint8_t> all;
    for (int k = 0; k < 3; k++) {
        ch[0] = (uint16_t)(500 + k);
        std::vector<uint8_t> f = channels_frame(ch);
        all.insert(all.end(), f.begin(), f.end());
    }
    for (uint8_t b : all) { io.feed({b}); crsf.read_packets(0); }
    TEST_ASSERT_EQUAL_UINT32(3, crsf.getValidFrames());
    TEST_ASSERT_EQUAL_UINT32(0, crsf.getCrcErrors());
    TEST_ASSERT_EQUAL_UINT16(502, crsf.get_crfs_channels(0));
}

static void test_parser_rejects_bad_crc_and_resyncs(void) {
    uint16_t ch[16] = {0};
    std::vector<uint8_t> bad = channels_frame(ch);
    bad.back() ^= 0xFF;
    ch[3] = 999;
    std::vector<uint8_t> good = channels_frame(ch);
    io.feed(bad);
    io.feed(good);
    crsf.read_packets(0);
    TEST_ASSERT_EQUAL_UINT32(1, crsf.getCrcErrors());
    TEST_ASSERT_EQUAL_UINT32(1, crsf.getValidFrames());
    TEST_ASSERT_EQUAL_UINT16(999, crsf.get_crfs_channels(3));
}

static void test_parser_discards_invalid_length(void) {
    // Laenge 0, 1 und 63 sind laut Spezifikation ungueltig (2..62).
    io.feed({0xC8, 0x00, 0xC8, 0x01, 0xC8, 63});
    uint16_t ch[16] = {0};
    ch[0] = 1234;
    io.feed(channels_frame(ch));
    crsf.read_packets(0);
    TEST_ASSERT_EQUAL_UINT32(1, crsf.getValidFrames());
    TEST_ASSERT_EQUAL_UINT16(1234, crsf.get_crfs_channels(0));
}

// ── Empfangene Frames ──────────────────────────────────────────────────

static void test_link_statistics_field_order(void) {
    // 0x14 laut Spezifikation: rssi1, rssi2, lq, snr, antenna, rf_profile,
    // rf_power, down_rssi, down_lq, down_snr
    io.feed(frame(CRSF_FRAMETYPE_LINK_STATISTICS,
                  {60, 61, 99, (uint8_t)-5, 1, 2, 3, 70, 98, (uint8_t)-7}));
    crsf.read_packets(0);
    const LinkStatistics& ls = crsf.getLinkStatistics();
    TEST_ASSERT_EQUAL_UINT8(60, ls.uplinkRSSI);
    TEST_ASSERT_EQUAL_UINT8(61, ls.uplinkRSSI2);
    TEST_ASSERT_EQUAL_UINT8(99, ls.uplinkLQ);
    TEST_ASSERT_EQUAL_INT8(-5, ls.uplinkSNR);
    TEST_ASSERT_EQUAL_UINT8(1, ls.activeAntenna);
    TEST_ASSERT_EQUAL_UINT8(2, ls.rfMode);
    TEST_ASSERT_EQUAL_UINT8(3, ls.txPower);
    TEST_ASSERT_EQUAL_UINT8(70, ls.downlinkRSSI);
    TEST_ASSERT_EQUAL_UINT8(98, ls.downlinkLQ);
    TEST_ASSERT_EQUAL_INT8(-7, ls.downlinkSNR);
}

static void test_ping_only_for_own_address_or_broadcast(void) {
    crsf.setDeviceAddress(0xC4);
    io.feed(frame(CRSF_FRAMETYPE_DEVICE_PING, {0xC6, 0xEA}));   // fremd
    crsf.read_packets(0);
    TEST_ASSERT_FALSE(crsf.getDeviceInfoReplyPending());
    io.feed(frame(CRSF_FRAMETYPE_DEVICE_PING, {0x00, 0xEA}));   // Broadcast
    crsf.read_packets(0);
    TEST_ASSERT_TRUE(crsf.getDeviceInfoReplyPending());
    TEST_ASSERT_EQUAL_HEX8(0xEA, crsf.getPingSource());
}

static void test_parameter_write_is_stored(void) {
    crsf.setDeviceAddress(0xC4);
    io.feed(frame(CRSF_FRAMETYPE_PARAMETER_WRITE, {0xC4, 0xEF, 7, 42}, 0xEE));
    crsf.read_packets(0);
    TEST_ASSERT_TRUE(crsf.getDeviceWriteReplyPending());
    TEST_ASSERT_EQUAL_UINT8(7, crsf.getParamWriteIndex());
    TEST_ASSERT_EQUAL_UINT8(42, crsf.getParamWriteValue());
}

// ── Gesendete Frames ───────────────────────────────────────────────────

static void test_device_info_layout(void) {
    crsf.setDeviceAddress(0xC4);
    crsf.send_device_info("Test", 12, 0x37);
    const std::vector<uint8_t>& f = io.tx;
    assert_valid_frame(f);
    TEST_ASSERT_EQUAL_HEX8(CRSF_FRAMETYPE_DEVICE_INFO, f[2]);
    TEST_ASSERT_EQUAL_HEX8(0xC4, f[4]);
    TEST_ASSERT_EQUAL_STRING("Test", (const char*)&f[5]);
    // nach Name (5 Byte inkl. 0): Seriennr, HW-ID, FW-ID (je 4), Anzahl, Version
    TEST_ASSERT_EQUAL_UINT8(12, f[5 + 5 + 12]);
    TEST_ASSERT_EQUAL_HEX8(0x37, f[5 + 5 + 13]);
}

static void test_folder_length_without_extra_byte(void) {
    crsf.send_param_response_CRSF_FOLDER(0, 0, "ROOT", {1, 2, 3});
    const std::vector<uint8_t>& f = io.tx;
    assert_valid_frame(f);
    // 7 Kopf-Bytes + "ROOT\0" + 3 Kinder + 0xFF + CRC = 17
    TEST_ASSERT_EQUAL_UINT8(17, f[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, f[f.size() - 2]);
}

static void assert_command_crc(const std::vector<uint8_t>& f) {
    assert_valid_frame(f);
    TEST_ASSERT_EQUAL_HEX8(CRSF_FRAMETYPE_COMMAND, f[2]);
    // Command-CRC steht vor der Haupt-CRC und deckt Typ bis Payload ab.
    size_t cmdCrcPos = f.size() - 2;
    TEST_ASSERT_EQUAL_HEX8(crc_ref(0xBA, &f[2], cmdCrcPos - 2), f[cmdCrcPos]);
}

static void test_mwset_command_crc(void) {
    crsf.send_command_MWSET(3, 0x55);
    assert_command_crc(io.tx);
}

static void test_mwprop_has_command_crc(void) {
    crsf.send_command_MWPROP(3, 1, 200);
    TEST_ASSERT_EQUAL_UINT8(10, io.tx[1]);
    TEST_ASSERT_EQUAL_UINT8(200, io.tx[9]);
    assert_command_crc(io.tx);
}

static void test_text_selection_chunks(void) {
    // Optionen > 55 Byte werden in Chunks aufgeteilt; jeder Chunk ist ein
    // gueltiger Frame <= 64 Byte, "chunks remaining" zaehlt herunter.
    std::string opts;
    for (int i = 0; i < 12; i++) opts += "Option" + std::to_string(i) + ";";
    crsf.send_param_response_CRSF_TEXT_SELECTION(5, 0, "Auswahl", opts.c_str(), 1, 0, 11);
    size_t pos = 0;
    int frames = 0;
    int lastRemaining = -1;
    while (pos < io.tx.size()) {
        size_t n = io.tx[pos + 1] + 2;
        TEST_ASSERT_TRUE(n <= 64);
        std::vector<uint8_t> f(io.tx.begin() + pos, io.tx.begin() + pos + n);
        assert_valid_frame(f);
        if (lastRemaining >= 0) TEST_ASSERT_EQUAL_INT(lastRemaining - 1, f[6]);
        lastRemaining = f[6];
        pos += n;
        frames++;
    }
    TEST_ASSERT_TRUE(frames >= 2);
    TEST_ASSERT_EQUAL_INT(0, lastRemaining);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_crc8_d5_matches_reference);
    RUN_TEST(test_crc8_ba_matches_spec_polynom);
    RUN_TEST(test_parser_reads_whole_frame);
    RUN_TEST(test_parser_completes_frame_split_across_calls);
    RUN_TEST(test_parser_byte_by_byte_several_frames);
    RUN_TEST(test_parser_rejects_bad_crc_and_resyncs);
    RUN_TEST(test_parser_discards_invalid_length);
    RUN_TEST(test_link_statistics_field_order);
    RUN_TEST(test_ping_only_for_own_address_or_broadcast);
    RUN_TEST(test_parameter_write_is_stored);
    RUN_TEST(test_device_info_layout);
    RUN_TEST(test_folder_length_without_extra_byte);
    RUN_TEST(test_mwset_command_crc);
    RUN_TEST(test_mwprop_has_command_crc);
    RUN_TEST(test_text_selection_chunks);
    return UNITY_END();
}
