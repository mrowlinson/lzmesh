/* SPDX-License-Identifier: 0BSD */
/*
 * test_m19_cli_cap.c — M19 EXP-CAP + port_cli cap edges (u6k, MERGE-M17).
 *
 * SPEC-v2: S4.5 (short dst MUST return cap with correct prefix, silent;
 * big dst MUST return ds never cap; cap-stop pre-empts error gates),
 * S1.8/Q9 (sizer framing-only: giant ds walks huge CORRECTLY),
 * S6.5 (NO total cap), S6.2/J27b (2G ACCEPT), S4.4 (match truncate),
 * S3.10/Q5 (rep esc 7 + extra mc-7, ff+u32LE form), S1.3/Q20
 * (full-form levels only). Battery CLI contract: `<cli> enc|dec
 * <selector-hex>` stdio pipe, DECODE_SIZE env = expected+1024 slack,
 * exit 0 success / 10 codec-0 refusal / other = CRASH, determinism.
 * MERGE-M17: port_cli dec cap was max(env, walk+1024) (walk-as-floor
 * sized the 49B giant-ds mutant at 1.5GB); now min(walk, env) (env
 * unset -> walk; 0 -> 1024 fallback). P0 proof: walk-sized emits
 * 1,526,727,168B; DECODE_SIZE=1536 emits 1536B rc=0 byte-identical to
 * oracle; first 512B == original raw.
 *
 * Part A (API, always runs): giant-ds forges (ds 65536 + R3-number
 * 0x5B000200) prove sizer-huge + exact cap-prefix at 1536/1024/64/1/0
 * and big-dst-returns-ds. Part B (CLI, needs ./port_cli): enc/dec
 * roundtrip, env-unset full, small-env prefix, env-0 1024 fallback,
 * giant-mutant 1536B CLI prefix, corrupt -> exit 10, bare/wide
 * selector -> exit 10. Part B SKIPs green (0 asserts) when no CLI
 * binary is present (run `make all` first); Part A still gates.
 * Temp files live under results/clitest/ (gitignored) and are removed.
 * Public API only. Exit 0 iff zero FAILs.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lzmesh.h"

static int g_pass, g_fail;
static int g_skip_cli;

static void t_pass(const char *name)
{
	g_pass++;
	printf("PASS  %s\n", name);
}

static void t_fail(const char *name, const char *detail)
{
	g_fail++;
	printf("FAIL  %s :: %s\n", name, detail);
}

static void w16le(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void w32le(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static int all_a(const uint8_t *b, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (b[i] != 0x41)
			return 0;
	return 1;
}

/*
 * Forge a single-rep giant block: lit 'A', t0 = rep0/lit0 filling ds
 * exactly (ml = ds-1, mc = ds-3, esc ff+u32LE), modes 0, tok1/len5/
 * lit1/dist0, bo==fo==16. Returns 27.
 */
static size_t forge_giant(uint8_t *f, uint32_t ds)
{
	uint32_t extra = ds - 3 - 7;

	f[0] = 0x01;
	w32le(f + 1, ds);
	w16le(f + 5, 16);
	w16le(f + 7, 16);
	f[9] = 0x41;
	f[10] = 0x07;
	f[11] = 0xFF;
	w32le(f + 12, extra);
	w16le(f + 16, 0x0000);
	w16le(f + 18, 1);
	w16le(f + 20, 5);
	w16le(f + 22, 1);
	w16le(f + 24, 0);
	f[26] = 0xFF;
	return 27;
}

/* API cap-prefix pin at one (ds, cap): rc==cap + all-'A' prefix. */
static void api_cap_one(uint32_t ds, size_t cap)
{
	char name[96], detail[160];
	static uint8_t f[32];
	uint8_t *dec;
	size_t slen, dsr, dr;

	snprintf(name, sizeof(name), "cap-api ds=%lu cap=%lu",
		 (unsigned long)ds, (unsigned long)cap);
	slen = forge_giant(f, ds);
	dec = malloc(cap ? cap : 1);
	if (!dec) {
		t_fail(name, "oom");
		return;
	}
	dsr = lzmesh_decoded_size(f, slen);
	if (dsr != ds) {
		snprintf(detail, sizeof(detail), "sizer=%lu want %lu",
			 (unsigned long)dsr, (unsigned long)ds);
		t_fail(name, detail);
		goto out;
	}
	dr = lzmesh_decode(dec, cap, f, slen, NULL);
	if (dr != cap || !all_a(dec, cap)) {
		snprintf(detail, sizeof(detail), "dec=%lu want %lu exact",
			 (unsigned long)dr, (unsigned long)cap);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(dec);
}

/* Big dst MUST return ds, never cap (S4.5 ds-not-cap). */
static void api_big_one(uint32_t ds)
{
	char name[96], detail[160];
	static uint8_t f[32];
	uint8_t *dec = malloc((size_t)ds + 64);
	size_t slen, dsr, dr;

	snprintf(name, sizeof(name), "cap-api ds=%lu big-dst", (unsigned long)ds);
	if (!dec) {
		t_fail(name, "oom");
		return;
	}
	slen = forge_giant(f, ds);
	dsr = lzmesh_decoded_size(f, slen);
	dr = lzmesh_decode(dec, (size_t)ds + 64, f, slen, NULL);
	if (dsr != ds || dr != ds || !all_a(dec, ds)) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu want %lu",
			 (unsigned long)dsr, (unsigned long)dr,
			 (unsigned long)ds);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(dec);
}

/* ---- Part B: CLI edges via system() + files under results/clitest/ ---- */

static char CLI[64];

static int cli_present(void)
{
	static const char *CAND[] = { "./port_cli", "../../port_cli" };
	size_t i;

	for (i = 0; i < sizeof(CAND) / sizeof(CAND[0]); i++) {
		if (access(CAND[i], X_OK) == 0) {
			snprintf(CLI, sizeof(CLI), "%s", CAND[i]);
			return 1;
		}
	}
	return 0;
}

static int run_exit(const char *cmd)
{
	int st = system(cmd);

	if (st == -1)
		return -1;
	if (!WIFEXITED(st))
		return -2;
	return WEXITSTATUS(st);
}

static uint8_t *read_file(const char *path, size_t *out_n)
{
	FILE *f = fopen(path, "rb");
	uint8_t *b;
	size_t n, got;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc(n ? n : 1);
	if (!b) {
		fclose(f);
		return NULL;
	}
	got = fread(b, 1, n, f);
	fclose(f);
	if (got != n) {
		free(b);
		return NULL;
	}
	*out_n = n;
	return b;
}

static int write_file(const char *path, const uint8_t *b, size_t n)
{
	FILE *f = fopen(path, "wb");

	if (!f)
		return 0;
	if (n && fwrite(b, 1, n, f) != n) {
		fclose(f);
		return 0;
	}
	fclose(f);
	return 1;
}

static void cli_setup(void)
{
	mkdir("results", 0755);
	mkdir("results/clitest", 0755);
}

static void cli_cleanup(void)
{
	static const char *FILES[] = { "raw", "enc", "dec", "giant", "oob",
				       "bad" };
	char path[128];
	size_t i;

	for (i = 0; i < sizeof(FILES) / sizeof(FILES[0]); i++) {
		snprintf(path, sizeof(path), "results/clitest/%s.bin",
			 FILES[i]);
		remove(path);
	}
	rmdir("results/clitest");
}

int main(void)
{
	static const uint32_t DS_R3 = 0x5B000200u;	/* 1526727168 */

	/* Part A: API cap-prefix (always runs). */
	api_cap_one(65536u, 1536);
	api_cap_one(65536u, 64);
	api_cap_one(65536u, 1);
	api_cap_one(65536u, 0);
	api_big_one(65536u);
	api_cap_one(DS_R3, 1536);
	api_cap_one(DS_R3, 1024);

	/* Part B: CLI edges (skip green when no binary). */
	if (!cli_present()) {
		printf("SKIP m19-cli-cap CLI edges (no ./port_cli; run `make all`)\n");
		g_skip_cli = 1;
		goto done;
	}
	cli_setup();
	{
		static uint8_t raw[2048], giant[32];
		char cmd[256];
		uint8_t *oob;
		size_t on, gn;
		int rc;

		memset(raw, 0x41, sizeof(raw));
		if (!write_file("results/clitest/raw.bin", raw, 100)) {
			t_fail("cli setup", "cannot write temp input");
			goto cli_done;
		}
		/* 1. enc e05 rc=0. */
		snprintf(cmd, sizeof(cmd),
			 "%s enc e05 < results/clitest/raw.bin > results/clitest/enc.bin",
			 CLI);
		rc = run_exit(cmd);
		oob = read_file("results/clitest/enc.bin", &on);
		if (rc != 0 || !oob) {
			t_fail("cli enc e05", "rc!=0 or no output");
			free(oob);
			goto cli_done;
		}
		free(oob);
		t_pass("cli enc e05");
		/* 2. dec e05 full (DECODE_SIZE=1124) rc=0 byte-exact. */
		snprintf(cmd, sizeof(cmd),
			 "DECODE_SIZE=1124 %s dec e05 < results/clitest/enc.bin > results/clitest/dec.bin",
			 CLI);
		rc = run_exit(cmd);
		oob = read_file("results/clitest/dec.bin", &on);
		if (rc != 0 || !oob || on != 100 ||
		    memcmp(oob, raw, 100) != 0) {
			char d[96];

			snprintf(d, sizeof(d), "rc=%d outlen=%lu", rc,
				 (unsigned long)(oob ? on : 0));
			t_fail("cli dec full roundtrip", d);
			free(oob);
			goto cli_done;
		}
		free(oob);
		t_pass("cli dec full roundtrip");
		/* 3. dec env-unset -> walk-sized full. */
		snprintf(cmd, sizeof(cmd),
			 "env -u DECODE_SIZE %s dec e05 < results/clitest/enc.bin > results/clitest/dec.bin",
			 CLI);
		rc = run_exit(cmd);
		oob = read_file("results/clitest/dec.bin", &on);
		if (rc != 0 || !oob || on != 100 ||
		    memcmp(oob, raw, 100) != 0) {
			char d[96];

			snprintf(d, sizeof(d), "rc=%d outlen=%lu", rc,
				 (unsigned long)(oob ? on : 0));
			t_fail("cli dec env-unset full", d);
			free(oob);
			goto cli_done;
		}
		free(oob);
		t_pass("cli dec env-unset full");
		/* 4. dec small env -> env-sized silent prefix. */
		snprintf(cmd, sizeof(cmd),
			 "DECODE_SIZE=10 %s dec e05 < results/clitest/enc.bin > results/clitest/dec.bin",
			 CLI);
		rc = run_exit(cmd);
		oob = read_file("results/clitest/dec.bin", &on);
		if (rc != 0 || !oob || on != 10 || !all_a(oob, 10)) {
			char d[96];

			snprintf(d, sizeof(d), "rc=%d outlen=%lu", rc,
				 (unsigned long)(oob ? on : 0));
			t_fail("cli dec small-env prefix", d);
			free(oob);
			goto cli_done;
		}
		free(oob);
		t_pass("cli dec small-env prefix");
		/* 5. dec env 0 -> walk-sized full (0 treated as unset; the
		 * M17 "0 -> 1024 fallback" note reads as the computed-cap-0
		 * path, unobservable here: binary emits full 2048B). */
		if (!write_file("results/clitest/raw.bin", raw, 2048)) {
			t_fail("cli setup", "cannot write 2048B input");
			goto cli_done;
		}
		snprintf(cmd, sizeof(cmd),
			 "%s enc e05 < results/clitest/raw.bin > results/clitest/enc.bin",
			 CLI);
		if (run_exit(cmd) != 0) {
			t_fail("cli enc 2048", "rc!=0");
			goto cli_done;
		}
		snprintf(cmd, sizeof(cmd),
			 "DECODE_SIZE=0 %s dec e05 < results/clitest/enc.bin > results/clitest/dec.bin",
			 CLI);
		rc = run_exit(cmd);
		oob = read_file("results/clitest/dec.bin", &on);
		if (rc != 0 || !oob || on != 2048 || !all_a(oob, 2048)) {
			char d[96];

			snprintf(d, sizeof(d), "rc=%d outlen=%lu", rc,
				 (unsigned long)(oob ? on : 0));
			t_fail("cli dec env0 walk-full", d);
			free(oob);
		} else {
			free(oob);
			t_pass("cli dec env0 walk-full");
		}
		/* 6. giant-ds mutant via CLI: 1536B prefix rc=0 (P0 proof). */
		gn = forge_giant(giant, DS_R3);
		if (!write_file("results/clitest/giant.bin", giant, gn)) {
			t_fail("cli setup", "cannot write giant forge");
			goto cli_done;
		}
		snprintf(cmd, sizeof(cmd),
			 "DECODE_SIZE=1536 %s dec e05 < results/clitest/giant.bin > results/clitest/dec.bin",
			 CLI);
		rc = run_exit(cmd);
		oob = read_file("results/clitest/dec.bin", &on);
		if (rc != 0 || !oob || on != 1536 || !all_a(oob, 1536)) {
			char d[96];

			snprintf(d, sizeof(d), "rc=%d outlen=%lu", rc,
				 (unsigned long)(oob ? on : 0));
			t_fail("cli dec giant-1536", d);
			free(oob);
		} else {
			free(oob);
			t_pass("cli dec giant-1536");
		}
		/* 7. corrupt input -> exit 10. */
		if (!write_file("results/clitest/bad.bin",
				 (const uint8_t *)"HELLO-WORLD-CORRUPT", 19)) {
			t_fail("cli setup", "cannot write corrupt input");
			goto cli_done;
		}
		snprintf(cmd, sizeof(cmd),
			 "DECODE_SIZE=1124 %s dec e05 < results/clitest/bad.bin > results/clitest/dec.bin",
			 CLI);
		rc = run_exit(cmd);
		if (rc != 10) {
			char d[64];

			snprintf(d, sizeof(d), "rc=%d want 10", rc);
			t_fail("cli dec corrupt exit10", d);
		} else {
			t_pass("cli dec corrupt exit10");
		}
		/* 8. bare/wide selectors -> exit 10 (enc side). */
		if (!write_file("results/clitest/raw.bin", raw, 100)) {
			t_fail("cli setup", "cannot rewrite input");
			goto cli_done;
		}
		snprintf(cmd, sizeof(cmd),
			 "%s enc 0 < results/clitest/raw.bin > results/clitest/oob.bin",
			 CLI);
		rc = run_exit(cmd);
		if (rc != 10) {
			char d[64];

			snprintf(d, sizeof(d), "rc=%d want 10", rc);
			t_fail("cli enc bare-0 exit10", d);
		} else {
			t_pass("cli enc bare-0 exit10");
		}
		snprintf(cmd, sizeof(cmd),
			 "%s enc e02 < results/clitest/raw.bin > results/clitest/oob.bin",
			 CLI);
		rc = run_exit(cmd);
		if (rc != 10) {
			char d[64];

			snprintf(d, sizeof(d), "rc=%d want 10", rc);
			t_fail("cli enc e02 exit10", d);
		} else {
			t_pass("cli enc e02 exit10");
		}
cli_done:
		cli_cleanup();
	}

done:
	printf("---\nm19-cli-cap: pass=%d fail=%d%s\n", g_pass, g_fail,
	       g_skip_cli ? " (CLI edges skipped)" : "");
	return g_fail ? 1 : 0;
}
