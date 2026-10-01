/*
 * C test runner (corec app_main/_start convention): the same program runs
 * natively, under Wasmtime and under Node with corec's WASI host.
 *
 *   ms_tests [--suite NAME[,NAME...]]... [--list]
 *
 * NAME is runtime, bigint, game, probability, posterior, planner, autosolve,
 * api or all.
 * Without --suite every suite runs. scripts/build.mjs compiles a suite in
 * (defining MS_TEST_SUITE_<NAME>=1) only together with its sources, so peers
 * can build subsets while modules are in progress; requesting a suite that
 * is not compiled in - including through the default/all - is an error
 * (exit 2), never a silent skip. Test failures exit 1.
 */

#include "test_support.h"

#if MS_TEST_SUITE_RUNTIME
void test_runtime(void);
#define MS_RUN_RUNTIME test_runtime
#else
#define MS_RUN_RUNTIME NULL
#endif
#if MS_TEST_SUITE_BIGINT
void test_bigint(void);
#define MS_RUN_BIGINT test_bigint
#else
#define MS_RUN_BIGINT NULL
#endif
#if MS_TEST_SUITE_GAME
void test_game(void);
#define MS_RUN_GAME test_game
#else
#define MS_RUN_GAME NULL
#endif
#if MS_TEST_SUITE_PROBABILITY
void test_probability(void);
#define MS_RUN_PROBABILITY test_probability
#else
#define MS_RUN_PROBABILITY NULL
#endif
#if MS_TEST_SUITE_POSTERIOR
void test_posterior(void);
#define MS_RUN_POSTERIOR test_posterior
#else
#define MS_RUN_POSTERIOR NULL
#endif
#if MS_TEST_SUITE_PLANNER
void test_planner(void);
#define MS_RUN_PLANNER test_planner
#else
#define MS_RUN_PLANNER NULL
#endif
#if MS_TEST_SUITE_AUTOSOLVE
void test_autosolve(void);
#define MS_RUN_AUTOSOLVE test_autosolve
#else
#define MS_RUN_AUTOSOLVE NULL
#endif
#if MS_TEST_SUITE_API
void test_api(void);
#define MS_RUN_API test_api
#else
#define MS_RUN_API NULL
#endif

typedef struct test_suite {
    const char *name;
    void (*run)(void);
} test_suite;

static const test_suite SUITES[] = {
    {"runtime", MS_RUN_RUNTIME},
    {"bigint", MS_RUN_BIGINT},
    {"game", MS_RUN_GAME},
    {"probability", MS_RUN_PROBABILITY},
    {"posterior", MS_RUN_POSTERIOR},
    {"planner", MS_RUN_PLANNER},
    {"autosolve", MS_RUN_AUTOSOLVE},
    {"api", MS_RUN_API},
};

#define SUITE_COUNT array_size(SUITES)

static int usage_error(const char *message, const char *detail) {
    test_print("error: ");
    test_print(message);
    if (detail) {
        test_print(": ");
        test_print(detail);
    }
    test_print("\nusage: ms_tests [--suite runtime|bigint|game|probability|posterior|planner|"
               "autosolve|api|all[,...]]... [--list]\n");
    return 2;
}

/* Marks the suites named in a comma-separated list; false on a bad name. */
static bool select_suites(char *list, bool selected[SUITE_COUNT], const char **bad) {
    char *token = list;
    for (;;) {
        char *end = token;
        while (*end && *end != ',') end++;
        char saved = *end;
        *end = '\0';
        bool found = false;
        if (base_strcmp(token, "all") == 0) {
            for (size_t i = 0; i < SUITE_COUNT; i++) selected[i] = true;
            found = true;
        }
        for (size_t i = 0; i < SUITE_COUNT && !found; i++) {
            if (base_strcmp(token, SUITES[i].name) == 0) {
                selected[i] = true;
                found = true;
            }
        }
        if (!found) {
            *bad = token;
            return false;
        }
        if (saved == '\0') return true;
        token = end + 1;
    }
}

static int run(char **argv, size_t argc) {
    bool selected[SUITE_COUNT];
    bool any = false;
    bool list = false;
    const char *bad = NULL;
    for (size_t i = 0; i < SUITE_COUNT; i++) selected[i] = false;

    for (size_t i = 1; i < argc; i++) {
        char *arg = argv[i];
        char *names = NULL;
        if (base_strcmp(arg, "--list") == 0) {
            list = true;
            continue;
        }
        if (base_strcmp(arg, "--suite") == 0) {
            if (i + 1 >= argc) return usage_error("--suite needs a value", NULL);
            names = argv[++i];
        } else if (base_strncmp(arg, "--suite=", 8) == 0) {
            names = arg + 8;
        } else {
            return usage_error("unknown argument", arg);
        }
        if (!select_suites(names, selected, &bad)) return usage_error("unknown suite", bad);
        any = true;
    }
    if (!any) {
        for (size_t i = 0; i < SUITE_COUNT; i++) selected[i] = true;
    }

    if (list) {
        for (size_t i = 0; i < SUITE_COUNT; i++) {
            test_print(SUITES[i].name);
            test_print(SUITES[i].run ? "  built\n" : "  not built\n");
        }
        return 0;
    }

    bool missing = false;
    for (size_t i = 0; i < SUITE_COUNT; i++) {
        if (selected[i] && SUITES[i].run == NULL) {
            if (!missing) test_print("error: requested suite(s) not compiled into this binary:");
            test_print(" ");
            test_print(SUITES[i].name);
            missing = true;
        }
    }
    if (missing) {
        test_print("\nBuild them with: node scripts/build.mjs <target> --suite <names>\n");
        return 2;
    }

    uint64_t passed = 0;
    for (size_t i = 0; i < SUITE_COUNT; i++) {
        if (!selected[i]) continue;
        test_print("== suite ");
        test_print(SUITES[i].name);
        test_print("\n");
        SUITES[i].run();
        test_print("ok ");
        test_print(SUITES[i].name);
        test_print("\n");
        passed++;
    }
    test_print("=== ");
    test_print_u64(passed);
    test_print(" suite(s) passed ===\n");
    return 0;
}

int app_main(void) {
    size_t argc = 0;
    size_t buf_size = 0;
    if (platform_args_sizes_get(&argc, &buf_size) != 0) return usage_error("cannot read arguments", NULL);
    rt_mem mem;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    char **argv = (char **)rt_calloc(&mem, argc + 1, sizeof(char *));
    char *buf = (char *)rt_alloc(&mem, buf_size + 1);
    if (argv == NULL || buf == NULL) return usage_error("cannot store arguments", NULL);
    if (argc > 0 && platform_args_get(argv, buf) != 0) return usage_error("cannot read arguments", NULL);
    int status = run(argv, argc);
    rt_mem_dispose(&mem);
    return status;
}
