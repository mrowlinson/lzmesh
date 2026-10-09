/* SPDX-License-Identifier: 0BSD */
/*
 * test_version.c — LZMESH_VERSION_* coherence: STRING equals the
 * MAJOR.MINOR.PATCH triplet and carries no "dev" marker. Release
 * versions only; placeholder markers fail. Public API only.
 * Exit 0 iff zero FAILs.
 */
#include <stdio.h>
#include <string.h>

#include "lzmesh.h"

static int g_pass, g_fail;

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

int main(void)
{
	char want[64];
	char detail[160];

	snprintf(want, sizeof(want), "%d.%d.%d", LZMESH_VERSION_MAJOR,
		 LZMESH_VERSION_MINOR, LZMESH_VERSION_PATCH);
	if (strcmp(LZMESH_VERSION_STRING, want) != 0) {
		snprintf(detail, sizeof(detail), "STRING \"%s\" != \"%s\"",
			 LZMESH_VERSION_STRING, want);
		t_fail("version-string-matches-triplet", detail);
	} else {
		t_pass("version-string-matches-triplet");
	}

	if (strstr(LZMESH_VERSION_STRING, "dev") != NULL) {
		snprintf(detail, sizeof(detail), "STRING \"%s\" has dev",
			 LZMESH_VERSION_STRING);
		t_fail("version-string-no-dev", detail);
	} else {
		t_pass("version-string-no-dev");
	}

	printf("---\nversion: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
