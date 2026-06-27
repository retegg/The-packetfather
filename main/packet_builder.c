#include "packet_builder.h"

#include "esp_mac.h"

#include <stddef.h>
#include <string.h>

#define IEEE80211_FC_PROBE_REQUEST 0x0040U
#define IEEE80211_FC_PROBE_RESPONSE 0x0050U
#define IEEE80211_FC_BEACON 0x0080U
#define IEEE80211_FC_DEAUTH 0x00c0U
#define IEEE80211_MGMT_HEADER_LEN 24U
#define IEEE80211_SEQUENCE_OFFSET 22U
#define IEEE80211_DEFAULT_BEACON_INTERVAL 100U
#define IEEE80211_CAPABILITY_ESS 0x0001U
#define IEEE80211_REASON_UNSPECIFIED 1U

static const uint8_t PF_MAC_BROADCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t PF_SUPPORTED_RATES[] = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24};

static void write_le16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xffU);
    out[1] = (uint8_t)(value >> 8);
}

static void write_le64(uint8_t *out, uint64_t value)
{
    for (int i = 0; i < 8; i++) {
        out[i] = (uint8_t)(value >> (i * 8));
    }
}

static uint8_t ssid_len_clamped(const char *ssid)
{
    size_t len;

    if (ssid == NULL) {
        return 0;
    }

    len = strlen(ssid);
    if (len > 32U) {
        len = 32U;
    }

    return (uint8_t)len;
}

static bool append_ie(pf_frame_t *frame, uint8_t id, const uint8_t *data, uint8_t len)
{
    if (frame == NULL || !frame->ok || data == NULL || (frame->len + 2U + len) > PF_TX_MAX_FRAME_LEN) {
        if (frame != NULL) {
            frame->ok = false;
        }
        return false;
    }

    frame->bytes[frame->len++] = id;
    frame->bytes[frame->len++] = len;
    memcpy(frame->bytes + frame->len, data, len);
    frame->len += len;
    return true;
}

static bool append_ssid(pf_frame_t *frame, const char *ssid)
{
    return append_ie(frame, 0, (const uint8_t *)(ssid != NULL ? ssid : ""), ssid_len_clamped(ssid));
}

static bool append_supported_rates(pf_frame_t *frame)
{
    return append_ie(frame, 1, PF_SUPPORTED_RATES, (uint8_t)sizeof(PF_SUPPORTED_RATES));
}

static bool append_ds_channel(pf_frame_t *frame, uint8_t channel)
{
    return append_ie(frame, 3, &channel, 1);
}

static bool append_country(pf_frame_t *frame, const char *country)
{
    uint8_t country_ie[6] = {'E', 'S', ' ', 1, 13, 20};

    if (country != NULL && country[0] != '\0' && country[1] != '\0') {
        country_ie[0] = (uint8_t)country[0];
        country_ie[1] = (uint8_t)country[1];
        country_ie[2] = country[2] != '\0' ? (uint8_t)country[2] : ' ';
    }

    return append_ie(frame, 7, country_ie, (uint8_t)sizeof(country_ie));
}

static void init_frame(pf_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->ok = true;
}

static bool channel_valid(uint8_t channel)
{
    return channel >= 1U && channel <= 13U;
}

static const uint8_t *mac_or_broadcast(const uint8_t *mac)
{
    return mac != NULL ? mac : PF_MAC_BROADCAST;
}

static const uint8_t *mac_or_sender(const uint8_t *mac, const uint8_t sender[6])
{
    return mac != NULL ? mac : sender;
}

static bool write_mgmt_header(pf_frame_t *frame,
                              uint16_t frame_control,
                              const uint8_t addr1[6],
                              const uint8_t addr2[6],
                              const uint8_t addr3[6],
                              uint16_t sequence)
{
    if (frame == NULL || addr1 == NULL || addr2 == NULL || addr3 == NULL || !frame->ok) {
        if (frame != NULL) {
            frame->ok = false;
        }
        return false;
    }

    if (PF_TX_MAX_FRAME_LEN < IEEE80211_MGMT_HEADER_LEN) {
        frame->ok = false;
        return false;
    }

    write_le16(frame->bytes + 0, frame_control);
    write_le16(frame->bytes + 2, 0);
    memcpy(frame->bytes + 4, addr1, 6);
    memcpy(frame->bytes + 10, addr2, 6);
    memcpy(frame->bytes + 16, addr3, 6);
    write_le16(frame->bytes + IEEE80211_SEQUENCE_OFFSET, (uint16_t)((sequence & 0x0fffU) << 4));
    frame->len = IEEE80211_MGMT_HEADER_LEN;
    return true;
}

static bool append_fixed_beacon_fields(pf_frame_t *frame, uint16_t interval, uint16_t capability)
{
    if (frame == NULL || !frame->ok || (frame->len + 12U) > PF_TX_MAX_FRAME_LEN) {
        if (frame != NULL) {
            frame->ok = false;
        }
        return false;
    }

    write_le64(frame->bytes + frame->len, 0);
    frame->len += 8;
    write_le16(frame->bytes + frame->len, interval);
    frame->len += 2;
    write_le16(frame->bytes + frame->len, capability);
    frame->len += 2;
    return true;
}

pf_frame_t Beacon(const char *ssid,
                  const uint8_t sender_mac[6],
                  uint8_t channel,
                  const pf_beacon_options_t *options)
{
    pf_frame_t frame;
    const uint8_t *destination;
    const uint8_t *bssid;
    uint16_t sequence = 0;
    uint16_t interval = IEEE80211_DEFAULT_BEACON_INTERVAL;
    uint16_t capability = IEEE80211_CAPABILITY_ESS;
    const char *country = NULL;

    init_frame(&frame);

    if (sender_mac == NULL || !channel_valid(channel)) {
        frame.ok = false;
        return frame;
    }

    if (options != NULL) {
        sequence = options->sequence;
        interval = options->beacon_interval != 0 ? options->beacon_interval : interval;
        capability = options->capability_info != 0 ? options->capability_info : capability;
        country = options->country;
    }

    destination = options != NULL ? mac_or_broadcast(options->destination) : PF_MAC_BROADCAST;
    bssid = options != NULL ? mac_or_sender(options->bssid, sender_mac) : sender_mac;

    write_mgmt_header(&frame, IEEE80211_FC_BEACON, destination, sender_mac, bssid, sequence);
    append_fixed_beacon_fields(&frame, interval, capability);
    append_ssid(&frame, ssid);
    append_supported_rates(&frame);
    append_ds_channel(&frame, channel);
    append_country(&frame, country);
    return frame;
}

pf_frame_t ProbeRequest(const char *ssid,
                        const uint8_t sender_mac[6],
                        uint8_t channel,
                        const pf_probe_request_options_t *options)
{
    pf_frame_t frame;
    const uint8_t *destination;
    const uint8_t *bssid;
    uint16_t sequence = 0;

    init_frame(&frame);

    if (sender_mac == NULL || !channel_valid(channel)) {
        frame.ok = false;
        return frame;
    }

    if (options != NULL) {
        sequence = options->sequence;
    }

    destination = options != NULL ? mac_or_broadcast(options->destination) : PF_MAC_BROADCAST;
    bssid = options != NULL ? mac_or_broadcast(options->bssid) : PF_MAC_BROADCAST;

    write_mgmt_header(&frame, IEEE80211_FC_PROBE_REQUEST, destination, sender_mac, bssid, sequence);
    append_ssid(&frame, ssid);
    append_supported_rates(&frame);
    append_ds_channel(&frame, channel);
    return frame;
}

pf_frame_t ProbeResponse(const char *ssid,
                         const uint8_t sender_mac[6],
                         uint8_t channel,
                         const pf_probe_response_options_t *options)
{
    pf_frame_t frame;
    const uint8_t *destination;
    const uint8_t *bssid;
    uint16_t sequence = 0;
    uint16_t interval = IEEE80211_DEFAULT_BEACON_INTERVAL;
    uint16_t capability = IEEE80211_CAPABILITY_ESS;

    init_frame(&frame);

    if (sender_mac == NULL || !channel_valid(channel)) {
        frame.ok = false;
        return frame;
    }

    if (options != NULL) {
        sequence = options->sequence;
        interval = options->beacon_interval != 0 ? options->beacon_interval : interval;
        capability = options->capability_info != 0 ? options->capability_info : capability;
    }

    destination = options != NULL ? mac_or_broadcast(options->destination) : PF_MAC_BROADCAST;
    bssid = options != NULL ? mac_or_sender(options->bssid, sender_mac) : sender_mac;

    write_mgmt_header(&frame, IEEE80211_FC_PROBE_RESPONSE, destination, sender_mac, bssid, sequence);
    append_fixed_beacon_fields(&frame, interval, capability);
    append_ssid(&frame, ssid);
    append_supported_rates(&frame);
    append_ds_channel(&frame, channel);
    return frame;
}

pf_frame_t Deauth(const uint8_t sender_mac[6],
                  uint8_t channel,
                  const pf_deauth_options_t *options)
{
    pf_frame_t frame;
    const uint8_t *destination;
    const uint8_t *bssid;
    uint16_t sequence = 0;
    uint16_t reason = IEEE80211_REASON_UNSPECIFIED;

    init_frame(&frame);

    if (sender_mac == NULL || !channel_valid(channel)) {
        frame.ok = false;
        return frame;
    }

    if (options != NULL) {
        sequence = options->sequence;
        reason = options->reason_code != 0 ? options->reason_code : reason;
    }

    destination = options != NULL ? mac_or_broadcast(options->destination) : PF_MAC_BROADCAST;
    bssid = options != NULL ? mac_or_sender(options->bssid, sender_mac) : sender_mac;

    write_mgmt_header(&frame, IEEE80211_FC_DEAUTH, destination, sender_mac, bssid, sequence);

    if ((frame.len + 2U) > PF_TX_MAX_FRAME_LEN) {
        frame.ok = false;
        return frame;
    }

    write_le16(frame.bytes + frame.len, reason);
    frame.len += 2;
    return frame;
}

bool pf_get_esp_wifi_mac(uint8_t out_mac[6])
{
    return out_mac != NULL && esp_read_mac(out_mac, ESP_MAC_WIFI_STA) == ESP_OK;
}

bool pf_get_esp_packet_mac(uint8_t out_mac[6])
{
    if (!pf_get_esp_wifi_mac(out_mac)) {
        return false;
    }

    out_mac[0] |= 0x02U;
    out_mac[0] &= 0xfeU;
    return true;
}

bool pf_frame_set_sequence(pf_frame_t *frame, uint16_t sequence)
{
    if (frame == NULL || !frame->ok || frame->len < IEEE80211_MGMT_HEADER_LEN) {
        return false;
    }

    write_le16(frame->bytes + IEEE80211_SEQUENCE_OFFSET, (uint16_t)((sequence & 0x0fffU) << 4));
    return true;
}
