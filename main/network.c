#include "network.h"

#include <string.h>

static bool packet_is_ap_advertisement(const pf_packet_metadata_t *packet)
{
    return packet != NULL && packet->type == PF_PACKET_TYPE_MANAGEMENT &&
           (packet->subtype == 5 || packet->subtype == 8) && packet->has_addresses;
}

static bool bssid_looks_valid(const uint8_t *bssid)
{
    bool any_nonzero = false;
    bool any_not_ff = false;

    if (bssid == NULL) {
        return false;
    }

    for (size_t i = 0; i < 6; i++) {
        any_nonzero = any_nonzero || bssid[i] != 0x00;
        any_not_ff = any_not_ff || bssid[i] != 0xff;
    }

    return any_nonzero && any_not_ff;
}

static const uint8_t *packet_bssid(const pf_packet_metadata_t *packet)
{
    if (!packet_is_ap_advertisement(packet)) {
        return NULL;
    }

    return packet->addr3;
}

static pf_network_security_t classify_security(bool privacy, bool has_rsn, bool has_wpa, bool has_rsn_sae)
{
    if (has_rsn_sae) {
        return PF_NETWORK_SECURITY_WPA3;
    }

    if (has_rsn) {
        return PF_NETWORK_SECURITY_WPA2;
    }

    if (has_wpa) {
        return PF_NETWORK_SECURITY_WPA;
    }

    if (privacy) {
        return PF_NETWORK_SECURITY_WEP;
    }

    return PF_NETWORK_SECURITY_OPEN;
}

static pf_network_t *find_network(pf_network_list_t *list, const uint8_t *bssid)
{
    for (size_t i = 0; i < list->count; i++) {
        if (memcmp(list->items[i].bssid, bssid, sizeof(list->items[i].bssid)) == 0) {
            return &list->items[i];
        }
    }

    return NULL;
}

static pf_network_t *alloc_network(pf_network_list_t *list, const uint8_t *bssid, uint32_t packet_id)
{
    pf_network_t *network;

    if (list->count >= PF_NETWORK_LIST_CAPACITY) {
        list->dropped++;
        return NULL;
    }

    network = &list->items[list->count++];
    memset(network, 0, sizeof(*network));
    network->in_use = true;
    memcpy(network->bssid, bssid, sizeof(network->bssid));
    network->first_seen_packet_id = packet_id;
    network->security = PF_NETWORK_SECURITY_UNKNOWN;
    return network;
}

static void update_network_ssid(pf_network_t *network, const pf_packet_metadata_t *packet)
{
    if (!packet->has_ssid) {
        if (!network->has_ssid) {
            network->hidden_ssid = true;
        }
        return;
    }

    memcpy(network->ssid, packet->ssid, sizeof(network->ssid));
    network->ssid[sizeof(network->ssid) - 1] = '\0';
    network->has_ssid = true;
    network->hidden_ssid = false;
}

static void update_network_rssi(pf_network_t *network, const pf_packet_metadata_t *packet)
{
    if (!packet->has_rssi) {
        return;
    }

    network->last_rssi = packet->rssi;

    if (!network->has_rssi) {
        network->average_rssi = packet->rssi;
        network->has_rssi = true;
        return;
    }

    network->average_rssi = (int16_t)(((int32_t)network->average_rssi * 3 + packet->rssi) / 4);
}

static void update_network_security(pf_network_t *network, const pf_packet_metadata_t *packet)
{
    if (packet->has_capability_info) {
        network->privacy = packet->privacy;
    }

    network->has_rsn = network->has_rsn || packet->has_rsn;
    network->has_wpa = network->has_wpa || packet->has_wpa;
    network->has_rsn_sae = network->has_rsn_sae || packet->has_rsn_sae;

    if (packet->has_rsn_details) {
        network->has_rsn_details = true;
        network->rsn_version = packet->rsn_version;
        network->group_cipher = packet->group_cipher;
        network->pairwise_cipher = packet->pairwise_cipher;
        network->akm = packet->akm;
    }

    if (packet->has_rsn_capabilities) {
        network->has_rsn_capabilities = true;
        network->rsn_capabilities = packet->rsn_capabilities;
    }

    network->security = classify_security(network->privacy,
                                          network->has_rsn,
                                          network->has_wpa,
                                          network->has_rsn_sae);
}

void pf_network_list_init(pf_network_list_t *list)
{
    if (list == NULL) {
        return;
    }

    memset(list, 0, sizeof(*list));
}

bool pf_network_list_update_from_packet(pf_network_list_t *list, const pf_packet_metadata_t *packet)
{
    const uint8_t *bssid;
    pf_network_t *network;

    if (list == NULL || !packet_is_ap_advertisement(packet)) {
        return false;
    }

    bssid = packet_bssid(packet);

    if (!bssid_looks_valid(bssid)) {
        return false;
    }

    network = find_network(list, bssid);

    if (network == NULL) {
        network = alloc_network(list, bssid, packet->id);
    }

    if (network == NULL) {
        return false;
    }

    update_network_ssid(network, packet);

    if (packet->has_channel) {
        network->has_channel = true;
        network->primary_channel = packet->primary_channel;
    }

    update_network_rssi(network, packet);
    update_network_security(network, packet);

    if (packet->subtype == 8) {
        network->beacon_count++;
    } else if (packet->subtype == 5) {
        network->probe_response_count++;
    }

    network->last_seen_packet_id = packet->id;
    return true;
}

size_t pf_network_list_count(const pf_network_list_t *list)
{
    if (list == NULL) {
        return 0;
    }

    return list->count;
}

bool pf_network_list_get_copy(const pf_network_list_t *list, size_t index, pf_network_t *out)
{
    if (list == NULL || out == NULL || index >= list->count) {
        return false;
    }

    *out = list->items[index];
    return true;
}

const char *pf_network_security_name(pf_network_security_t security)
{
    switch (security) {
        case PF_NETWORK_SECURITY_OPEN:
            return "OPEN";
        case PF_NETWORK_SECURITY_WEP:
            return "WEP";
        case PF_NETWORK_SECURITY_WPA:
            return "WPA";
        case PF_NETWORK_SECURITY_WPA2:
            return "WPA2";
        case PF_NETWORK_SECURITY_WPA3:
            return "WPA3";
        case PF_NETWORK_SECURITY_UNKNOWN:
        default:
            return "?";
    }
}

const char *pf_rsn_cipher_name(pf_rsn_cipher_t cipher)
{
    switch (cipher) {
        case PF_RSN_CIPHER_NONE:
            return "NONE";
        case PF_RSN_CIPHER_WEP40:
            return "WEP40";
        case PF_RSN_CIPHER_TKIP:
            return "TKIP";
        case PF_RSN_CIPHER_CCMP:
            return "CCMP";
        case PF_RSN_CIPHER_WEP104:
            return "WEP104";
        case PF_RSN_CIPHER_BIP_CMAC_128:
            return "BIP-CMAC-128";
        case PF_RSN_CIPHER_GCMP:
            return "GCMP";
        case PF_RSN_CIPHER_GCMP_256:
            return "GCMP-256";
        case PF_RSN_CIPHER_CCMP_256:
            return "CCMP-256";
        case PF_RSN_CIPHER_UNKNOWN:
        default:
            return "?";
    }
}

const char *pf_rsn_akm_name(pf_rsn_akm_t akm)
{
    switch (akm) {
        case PF_RSN_AKM_8021X:
            return "802.1X";
        case PF_RSN_AKM_PSK:
            return "PSK";
        case PF_RSN_AKM_FT_8021X:
            return "FT-802.1X";
        case PF_RSN_AKM_FT_PSK:
            return "FT-PSK";
        case PF_RSN_AKM_8021X_SHA256:
            return "802.1X-SHA256";
        case PF_RSN_AKM_PSK_SHA256:
            return "PSK-SHA256";
        case PF_RSN_AKM_SAE:
            return "SAE";
        case PF_RSN_AKM_FT_SAE:
            return "FT-SAE";
        case PF_RSN_AKM_OWE:
            return "OWE";
        case PF_RSN_AKM_UNKNOWN:
        default:
            return "?";
    }
}