// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int vita_boot_logging(void);
const char* vita_boot_session(void);
const char* vita_boot_tr(const char* key);
void vita_boot_language(const char* code);
void vita_boot_write(const char *text);
void vita_boot_client_write(const char *text);
void vita_boot_flush(void);
int vita_boot_stderr_write(void*, const char*, int);
void vita_boot_phase(const char *text);
void vita_boot_memory(const char *phase);
void vita_boot_options(int automatic, const char *fail_phase);
void vita_boot_wait(void);
void vita_boot_presented(void);
void vita_boot_fatal(const char *message) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
