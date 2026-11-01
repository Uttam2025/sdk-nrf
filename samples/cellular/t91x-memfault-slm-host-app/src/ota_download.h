/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef OTA_DOWNLOAD_H_
#define OTA_DOWNLOAD_H_

#include <stdint.h>

/** Path of the downloaded OTA bundle on the external flash file system. */
#define OTA_BUNDLE_PATH "/ota/bundle.bin"

/**
 * @brief Download an OTA bundle to OTA_BUNDLE_PATH.
 *
 * Blocks until the download completes or fails. Any previous bundle is removed first,
 * and a partial bundle is removed on failure.
 *
 * @param url            HTTP(S) URL of the bundle.
 * @param sec_tags       TLS security tags, or NULL for plain HTTP.
 * @param sec_tag_count  Number of entries in @p sec_tags.
 *
 * @retval 0 on success, negative errno on failure.
 */
int ota_bundle_download(const char *url, const int *sec_tags, uint8_t sec_tag_count);

/**
 * @brief Verify the downloaded bundle and apply its supported update elements.
 *
 * Verifies element metadata, payload size, and SHA-256 before any transfer.
 * Suspends the modem and PPP interface, streams application, modem delta, or
 * full-modem firmware using AT#XDFU, and restores networking after success.
 * A failed full-modem update leaves the modem suspended for retry.
 * Network registration completes asynchronously. Other targets are not applied.
 * The nRF91 application must confirm its new MCUboot image after booting.
 * nRF53 app/net images are staged in their MCUboot secondary slots in file order.
 * nRF53 bootloader images share app staging; a second app/bootloader write is
 * rejected with -EBUSY. MCUboot copies a bootloader candidate into the inactive
 * S0/S1 bank on reboot, without direct bank writes by this application.
 * After all supported transfers succeed, only the staged nRF53 images are
 * scheduled and the host reboots. No nRF53 image is scheduled on a staging error.
 *
 * @retval 0 on success, negative errno on failure.
 * @retval -ENOTSUP if the bundle contains no supported update element.
 */
int ota_bundle_apply(void);

#endif /* OTA_DOWNLOAD_H_ */