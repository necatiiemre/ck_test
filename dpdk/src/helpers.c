#include "helpers.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <rte_ethdev.h>
#include <rte_atomic.h>

#include "config.h"
#include "tx_rx_manager.h"  // rx_stats_per_port için
#include "dpdk_external_tx.h" // External TX stats için
#include "raw_socket_port.h"  // reset_raw_socket_stats için

// Daemon mode flag - when true, ANSI escape codes are disabled
bool g_daemon_mode = false;

void helper_set_daemon_mode(bool enabled) {
    g_daemon_mode = enabled;
}

// Yardımcı fonksiyonlar
static inline double to_gbps(uint64_t bytes) {
    return (bytes * 8.0) / 1e9;
}

void helper_reset_stats(const struct ports_config *ports_config,
                        uint64_t prev_tx_bytes[], uint64_t prev_rx_bytes[])
{
    // HW istatistiklerini resetle ve prev_* sayaçlarını sıfırla
    for (uint16_t i = 0; i < ports_config->nb_ports; i++) {
        uint16_t port_id = ports_config->ports[i].port_id;
        rte_eth_stats_reset(port_id);
        prev_tx_bytes[port_id] = 0;
        prev_rx_bytes[port_id] = 0;
    }

    // RX doğrulama istatistikleri (PRBS) sıfırla
    init_rx_stats();

#if STATS_MODE_DTN
    init_dtn_stats();
#endif

    // Raw socket ve global sequence tracking sıfırla
    reset_raw_socket_stats();
}

#if STATS_MODE_DTN
// ==========================================
// DTN PORT-BASED STATISTICS TABLE
// ==========================================
// 34 satır: DTN Port 0-31 (DPDK) + DTN Port 32 (Port12) + DTN Port 33 (Port13)
// Sütunlar: TX Pkts/Bytes/Gbps | RX Pkts/Bytes/Gbps | Good/Bad/Lost/BitErr/BER
//
// DTN TX (DTN→Server) = Server RX = HW q_ipackets[queue]
// DTN RX (Server→DTN) = Server TX = HW q_opackets[queue]
// PRBS = dtn_stats[dtn_port] (RX worker'dan)

// Per-queue prev bytes (DTN port bazlı delta hesaplama için)
// [dtn_port][0=tx_bytes, 1=rx_bytes]
static uint64_t dtn_prev_tx_bytes[DTN_PORT_COUNT];
static uint64_t dtn_prev_rx_bytes[DTN_PORT_COUNT];

static void helper_print_dtn_stats(const struct ports_config *ports_config,
                                   bool warmup_complete, unsigned loop_count,
                                   unsigned test_time)
{
    // Ekranı temizle
    if (!g_daemon_mode) {
        printf("\033[2J\033[H");
    } else {
        printf("\n========== [%s %u sn] ==========\n",
               warmup_complete ? "TEST" : "WARM-UP",
               warmup_complete ? test_time : loop_count);
    }

    // Başlık
    printf("╔══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╗\n");
    if (!warmup_complete) {
        printf("║                                                              DTN PORT STATS - WARM-UP (%3u/120 sn)                                                                                                                          ║\n", loop_count);
    } else {
        printf("║                                                              DTN PORT STATS - TEST Süresi: %5u sn                                                                                                                          ║\n", test_time);
    }
    printf("╚══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╝\n\n");

    // Tablo başlığı
    printf("┌──────┬─────────────────────────────────────────────────────────────────────┬─────────────────────────────────────────────────────────────────────┬───────────────────────────────────────────────────────────────────────────────────────────────────┐\n");
    printf("│ DTN  │                          DTN TX (DTN→Server)                        │                          DTN RX (Server→DTN)                        │                                      PRBS Doğrulama                                               │\n");
    printf("│ Port ├─────────────────────┬─────────────────────┬─────────────────────────┼─────────────────────┬─────────────────────┬─────────────────────────┼─────────────────────┬─────────────────────┬─────────────────────┬─────────────────────┬─────────────┤\n");
    printf("│      │       Packets       │        Bytes        │          Gbps           │       Packets       │        Bytes        │          Gbps           │        Good         │         Bad         │        Lost         │      Bit Error      │     BER     │\n");
    printf("├──────┼─────────────────────┼─────────────────────┼─────────────────────────┼─────────────────────┼─────────────────────┼─────────────────────────┼─────────────────────┼─────────────────────┼─────────────────────┼─────────────────────┼─────────────┤\n");

    // HW stats'leri bir kere çek (port başına)
    struct rte_eth_stats port_hw_stats[MAX_PORTS];
    for (uint16_t i = 0; i < ports_config->nb_ports; i++) {
        uint16_t port_id = ports_config->ports[i].port_id;
        if (rte_eth_stats_get(port_id, &port_hw_stats[port_id]) != 0) {
            memset(&port_hw_stats[port_id], 0, sizeof(struct rte_eth_stats));
        }
    }

    // DTN Port 0-31 (DPDK portları)
    for (uint16_t dtn = 0; dtn < DTN_DPDK_PORT_COUNT; dtn++) {
        const struct dtn_port_map_entry *entry = &dtn_port_map[dtn];

        // DTN TX (DTN→Server) = Software counters (raw socket hariç, sadece DTN paketleri)
        uint64_t good = rte_atomic64_read(&dtn_stats[dtn].good_pkts);
        uint64_t bad = rte_atomic64_read(&dtn_stats[dtn].bad_pkts);
        uint64_t dtn_tx_pkts = good + bad;
        uint64_t dtn_tx_bytes = rte_atomic64_read(&dtn_stats[dtn].internal_rx_bytes);

        // DTN RX (Server→DTN) = Server TX = HW q_opackets[queue] on rx_server_port
        uint16_t srv_tx_port = entry->rx_server_port;
        uint16_t srv_tx_queue = entry->rx_server_queue;
        uint64_t dtn_rx_pkts = port_hw_stats[srv_tx_port].q_opackets[srv_tx_queue];
        uint64_t dtn_rx_bytes = port_hw_stats[srv_tx_port].q_obytes[srv_tx_queue];

        // Gbps delta hesaplama
        uint64_t tx_delta = dtn_tx_bytes - dtn_prev_tx_bytes[dtn];
        uint64_t rx_delta = dtn_rx_bytes - dtn_prev_rx_bytes[dtn];
        double tx_gbps = to_gbps(tx_delta);
        double rx_gbps = to_gbps(rx_delta);

        // Prev güncelle
        dtn_prev_tx_bytes[dtn] = dtn_tx_bytes;
        dtn_prev_rx_bytes[dtn] = dtn_rx_bytes;

        // PRBS istatistikleri (dtn_stats'ten)
        uint64_t lost = rte_atomic64_read(&dtn_stats[dtn].lost_pkts);
        uint64_t bit_errors = rte_atomic64_read(&dtn_stats[dtn].bit_errors);

        // BER hesaplama
        double ber = 0.0;
        uint64_t total_bits = dtn_tx_bytes * 8;
        if (total_bits > 0) {
            ber = (double)bit_errors / (double)total_bits;
        }

        printf("│  %2u  │ %19lu │ %19lu │ %23.2f │ %19lu │ %19lu │ %23.2f │ %19lu │ %19lu │ %19lu │ %19lu │ %11.2e │\n",
               dtn,
               dtn_tx_pkts, dtn_tx_bytes, tx_gbps,
               dtn_rx_pkts, dtn_rx_bytes, rx_gbps,
               good, bad, lost, bit_errors, ber);
    }

    // DTN Port 32 (Port 12 - 1G raw socket)
    // DTN TX = DTN→Server = dpdk_ext_rx_stats (server bu port'tan alıyor)
    // DTN RX = Server→DTN = raw socket TX aggregate (server bu port'tan gönderiyor)
    {
        struct raw_socket_port *port12 = &raw_ports[0];
        // DTN TX: Server Port 12'den aldığı (DPDK External TX RX stats)
        pthread_spin_lock(&port12->dpdk_ext_rx_stats.lock);
        uint64_t dtn32_tx_pkts = port12->dpdk_ext_rx_stats.rx_packets;
        uint64_t dtn32_tx_bytes = port12->dpdk_ext_rx_stats.rx_bytes;
        uint64_t dtn32_good = port12->dpdk_ext_rx_stats.good_pkts;
        uint64_t dtn32_bad = port12->dpdk_ext_rx_stats.bad_pkts;
        uint64_t dtn32_bit_err = port12->dpdk_ext_rx_stats.bit_errors;
        pthread_spin_unlock(&port12->dpdk_ext_rx_stats.lock);

        // DTN RX: Server'ın Port 12 üzerinden gönderdiği (raw socket TX aggregate)
        uint64_t dtn32_rx_pkts = 0, dtn32_rx_bytes = 0;
        for (uint16_t t = 0; t < port12->tx_target_count; t++) {
            pthread_spin_lock(&port12->tx_targets[t].stats.lock);
            dtn32_rx_pkts += port12->tx_targets[t].stats.tx_packets;
            dtn32_rx_bytes += port12->tx_targets[t].stats.tx_bytes;
            pthread_spin_unlock(&port12->tx_targets[t].stats.lock);
        }

        uint64_t tx_delta = dtn32_tx_bytes - dtn_prev_tx_bytes[DTN_RAW_PORT_12];
        uint64_t rx_delta = dtn32_rx_bytes - dtn_prev_rx_bytes[DTN_RAW_PORT_12];
        dtn_prev_tx_bytes[DTN_RAW_PORT_12] = dtn32_tx_bytes;
        dtn_prev_rx_bytes[DTN_RAW_PORT_12] = dtn32_rx_bytes;
        double tx_gbps = to_gbps(tx_delta);
        double rx_gbps = to_gbps(rx_delta);

        uint64_t dtn32_lost = get_global_sequence_lost();

        double ber = 0.0;
        uint64_t total_bits = dtn32_tx_bytes * 8;
        if (total_bits > 0) ber = (double)dtn32_bit_err / (double)total_bits;

        printf("│  32  │ %19lu │ %19lu │ %23.2f │ %19lu │ %19lu │ %23.2f │ %19lu │ %19lu │ %19lu │ %19lu │ %11.2e │\n",
               dtn32_tx_pkts, dtn32_tx_bytes, tx_gbps,
               dtn32_rx_pkts, dtn32_rx_bytes, rx_gbps,
               dtn32_good, dtn32_bad, dtn32_lost, dtn32_bit_err, ber);
    }

    // DTN Port 33 (Port 13 - 100M raw socket)
    {
        struct raw_socket_port *port13 = &raw_ports[1];
        // DTN TX: Server Port 13'ten aldığı (DPDK External TX RX stats)
        pthread_spin_lock(&port13->dpdk_ext_rx_stats.lock);
        uint64_t dtn33_tx_pkts = port13->dpdk_ext_rx_stats.rx_packets;
        uint64_t dtn33_tx_bytes = port13->dpdk_ext_rx_stats.rx_bytes;
        uint64_t dtn33_good = port13->dpdk_ext_rx_stats.good_pkts;
        uint64_t dtn33_bad = port13->dpdk_ext_rx_stats.bad_pkts;
        uint64_t dtn33_bit_err = port13->dpdk_ext_rx_stats.bit_errors;
        pthread_spin_unlock(&port13->dpdk_ext_rx_stats.lock);

        // DTN RX: Server'ın Port 13 üzerinden gönderdiği
        uint64_t dtn33_rx_pkts = 0, dtn33_rx_bytes = 0;
        for (uint16_t t = 0; t < port13->tx_target_count; t++) {
            pthread_spin_lock(&port13->tx_targets[t].stats.lock);
            dtn33_rx_pkts += port13->tx_targets[t].stats.tx_packets;
            dtn33_rx_bytes += port13->tx_targets[t].stats.tx_bytes;
            pthread_spin_unlock(&port13->tx_targets[t].stats.lock);
        }

        uint64_t tx_delta = dtn33_tx_bytes - dtn_prev_tx_bytes[DTN_RAW_PORT_13];
        uint64_t rx_delta = dtn33_rx_bytes - dtn_prev_rx_bytes[DTN_RAW_PORT_13];
        dtn_prev_tx_bytes[DTN_RAW_PORT_13] = dtn33_tx_bytes;
        dtn_prev_rx_bytes[DTN_RAW_PORT_13] = dtn33_rx_bytes;
        double tx_gbps = to_gbps(tx_delta);
        double rx_gbps = to_gbps(rx_delta);

        uint64_t dtn33_lost = get_global_sequence_lost_p13();

        double ber = 0.0;
        uint64_t total_bits = dtn33_tx_bytes * 8;
        if (total_bits > 0) ber = (double)dtn33_bit_err / (double)total_bits;

        printf("│  33  │ %19lu │ %19lu │ %23.2f │ %19lu │ %19lu │ %23.2f │ %19lu │ %19lu │ %19lu │ %19lu │ %11.2e │\n",
               dtn33_tx_pkts, dtn33_tx_bytes, tx_gbps,
               dtn33_rx_pkts, dtn33_rx_bytes, rx_gbps,
               dtn33_good, dtn33_bad, dtn33_lost, dtn33_bit_err, ber);
    }

    printf("└──────┴─────────────────────┴─────────────────────┴─────────────────────────┴─────────────────────┴─────────────────────┴─────────────────────────┴─────────────────────┴─────────────────────┴─────────────────────┴─────────────────────┴─────────────┘\n");

    // DTN uyarılar
    bool has_warning = false;
    for (uint16_t dtn = 0; dtn < DTN_DPDK_PORT_COUNT; dtn++) {
        uint64_t bad = rte_atomic64_read(&dtn_stats[dtn].bad_pkts);
        uint64_t bit_err = rte_atomic64_read(&dtn_stats[dtn].bit_errors);
        uint64_t lost = rte_atomic64_read(&dtn_stats[dtn].lost_pkts);

        if (bad > 0 || bit_err > 0 || lost > 0) {
            if (!has_warning) {
                printf("\n  UYARILAR:\n");
                has_warning = true;
            }
            if (bad > 0)
                printf("      DTN Port %u: %lu bad paket!\n", dtn, bad);
            if (bit_err > 0)
                printf("      DTN Port %u: %lu bit hatası!\n", dtn, bit_err);
            if (lost > 0)
                printf("      DTN Port %u: %lu kayıp paket!\n", dtn, lost);
        }
    }

    printf("\n  Ctrl+C ile durdur\n");
}
#endif /* STATS_MODE_DTN */

// ==========================================
// SERVER PORT-BASED STATISTICS TABLE (Eski tablo)
// ==========================================
static void helper_print_server_stats(const struct ports_config *ports_config,
                                      const uint64_t prev_tx_bytes[],
                                      const uint64_t prev_rx_bytes[],
                                      bool warmup_complete, unsigned loop_count,
                                      unsigned test_time)
{
    // Ekranı temizle (sadece interaktif modda, daemon modda log dosyası için devre dışı)
    if (!g_daemon_mode) {
        printf("\033[2J\033[H");
    } else {
        // Daemon modda: Tablolar arasında ayırıcı satır
        printf("\n========== [%s %u sn] ==========\n",
               warmup_complete ? "TEST" : "WARM-UP",
               warmup_complete ? test_time : loop_count);
    }

    // Başlık (240 karakter genişlik)
    printf("╔══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╗\n");
    if (!warmup_complete) {
        printf("║                                                                    WARM-UP PHASE (%3u/120 sn) - İstatistikler 120 saniyede sıfırlanacak                                                                                        ║\n", loop_count);
    } else {
        printf("║                                                                    TEST DEVAM EDİYOR - Test Süresi: %5u sn                                                                                                                    ║\n", test_time);
    }
    printf("╚══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╝\n\n");

    // Ana istatistik tablosu (240 karakter)
    printf("┌──────┬─────────────────────────────────────────────────────────────────────┬─────────────────────────────────────────────────────────────────────┬───────────────────────────────────────────────────────────────────────────────────────────────────┐\n");
    printf("│ Port │                            TX (Gönderilen)                          │                            RX (Alınan)                              │                                      PRBS Doğrulama                                               │\n");
    printf("│      ├─────────────────────┬─────────────────────┬─────────────────────────┼─────────────────────┬─────────────────────┬─────────────────────────┼─────────────────────┬─────────────────────┬─────────────────────┬─────────────────────┬─────────────┤\n");
    printf("│      │       Packets       │        Bytes        │          Gbps           │       Packets       │        Bytes        │          Gbps           │        Good         │         Bad         │        Lost         │      Bit Error      │     BER     │\n");
    printf("├──────┼─────────────────────┼─────────────────────┼─────────────────────────┼─────────────────────┼─────────────────────┼─────────────────────────┼─────────────────────┼─────────────────────┼─────────────────────┼─────────────────────┼─────────────┤\n");

    struct rte_eth_stats st;

    for (uint16_t i = 0; i < ports_config->nb_ports; i++) {
        uint16_t port_id = ports_config->ports[i].port_id;

        if (rte_eth_stats_get(port_id, &st) != 0) {
            printf("│  %2u  │         N/A         │         N/A         │           N/A           │         N/A         │         N/A         │           N/A           │         N/A         │         N/A         │         N/A         │         N/A         │     N/A     │\n", port_id);
            continue;
        }

        // HW istatistikleri
        uint64_t tx_pkts = st.opackets;
        uint64_t tx_bytes = st.obytes;
        uint64_t rx_pkts = st.ipackets;
        uint64_t rx_bytes = st.ibytes;

        // Per-second rate hesaplama
        uint64_t tx_bytes_delta = tx_bytes - prev_tx_bytes[port_id];
        uint64_t rx_bytes_delta = rx_bytes - prev_rx_bytes[port_id];
        double tx_gbps = to_gbps(tx_bytes_delta);
        double rx_gbps = to_gbps(rx_bytes_delta);

        // PRBS doğrulama istatistikleri
        uint64_t good = rte_atomic64_read(&rx_stats_per_port[port_id].good_pkts);
        uint64_t bad = rte_atomic64_read(&rx_stats_per_port[port_id].bad_pkts);
        uint64_t lost = rte_atomic64_read(&rx_stats_per_port[port_id].lost_pkts);
        uint64_t bit_errors = rte_atomic64_read(&rx_stats_per_port[port_id].bit_errors);

        // Bit Error Rate (BER) hesaplama
        double ber = 0.0;
        uint64_t total_bits = rx_bytes * 8;
        if (total_bits > 0) {
            ber = (double)bit_errors / (double)total_bits;
        }

        // Tabloyu yazdır
        printf("│  %2u  │ %19lu │ %19lu │ %23.2f │ %19lu │ %19lu │ %23.2f │ %19lu │ %19lu │ %19lu │ %19lu │ %11.2e │\n",
               port_id,
               tx_pkts, tx_bytes, tx_gbps,
               rx_pkts, rx_bytes, rx_gbps,
               good, bad, lost, bit_errors, ber);
    }

    printf("└──────┴─────────────────────┴─────────────────────┴─────────────────────────┴─────────────────────┴─────────────────────┴─────────────────────────┴─────────────────────┴─────────────────────┴─────────────────────┴─────────────────────┴─────────────┘\n");

    // Uyarılar
    bool has_warning = false;
    for (uint16_t i = 0; i < ports_config->nb_ports; i++) {
        uint16_t port_id = ports_config->ports[i].port_id;

        uint64_t bad_pkts = rte_atomic64_read(&rx_stats_per_port[port_id].bad_pkts);
        uint64_t bit_errors = rte_atomic64_read(&rx_stats_per_port[port_id].bit_errors);
        uint64_t lost_pkts = rte_atomic64_read(&rx_stats_per_port[port_id].lost_pkts);

        if (bad_pkts > 0 || bit_errors > 0 || lost_pkts > 0) {
            if (!has_warning) {
                printf("\n  UYARILAR:\n");
                has_warning = true;
            }
            if (bad_pkts > 0) {
                printf("      Port %u: %lu bad paket tespit edildi!\n", port_id, bad_pkts);
            }
            if (bit_errors > 0) {
                printf("      Port %u: %lu bit hatası tespit edildi!\n", port_id, bit_errors);
            }
            if (lost_pkts > 0) {
                printf("      Port %u: %lu kayıp paket tespit edildi!\n", port_id, lost_pkts);
            }
        }

        // HW missed packets kontrolü
        struct rte_eth_stats st2;
        if (rte_eth_stats_get(port_id, &st2) == 0 && st2.imissed > 0) {
            if (!has_warning) {
                printf("\n  UYARILAR:\n");
                has_warning = true;
            }
            printf("      Port %u: %lu paket donanım tarafından kaçırıldı (imissed)!\n", port_id, st2.imissed);
        }
    }

    printf("\n  Ctrl+C ile durdur\n");
}

// ==========================================
// PUBLIC API: helper_print_stats
// ==========================================
// STATS_MODE_DTN flag'ine göre DTN veya Server tablosunu çizer

void helper_print_stats(const struct ports_config *ports_config,
                        const uint64_t prev_tx_bytes[], const uint64_t prev_rx_bytes[],
                        bool warmup_complete, unsigned loop_count, unsigned test_time)
{
#if STATS_MODE_DTN
    helper_print_dtn_stats(ports_config, warmup_complete, loop_count, test_time);
    (void)prev_tx_bytes;
    (void)prev_rx_bytes;
#else
    helper_print_server_stats(ports_config, prev_tx_bytes, prev_rx_bytes,
                              warmup_complete, loop_count, test_time);
#endif
}
