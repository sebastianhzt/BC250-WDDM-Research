/* SPDX-License-Identifier: Apache-2.0
 * Staged offline production port: active GPU/firmware/VM/submission forbidden.
 * This is NOT installed-driver or whole-tree hardware safety certification.
 * CPU metadata models remain standalone; no DMA/VRAM ownership is established.
 * Do not bypass with registry settings or compiler flags. Future enablement
 * requires a separately reviewed source change and real ownership evidence.
 */
#ifndef BC250_PASSIVE_POLICY_H
#define BC250_PASSIVE_POLICY_H
#ifdef BC250_PASSIVE_GPU_RUNTIME_ENABLED
#error Do not override the passive research policy with compiler flags
#endif
#define BC250_PASSIVE_GPU_RUNTIME_ENABLED 0U
#define BC250_PASSIVE_PORT_POLICY_VERSION 1U
#endif
