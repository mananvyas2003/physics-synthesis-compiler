#include "cli.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  if (strcmp(argv[1], "db") == 0)
    return cmd_db(argc, argv);

  if (strcmp(argv[1], "dfm") == 0)
    return cmd_dfm(argc, argv);

  if (strcmp(argv[1], "compile") == 0)
    return cmd_compile(argc, argv);

  if (strcmp(argv[1], "physics2") == 0)
    return cmd_physics2(argc, argv);

  if (strcmp(argv[1], "generate") == 0)
    return cmd_generate(argc, argv);

  if (strcmp(argv[1], "parse") == 0)
    return cmd_parse(argc, argv);

  if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 ||
      strcmp(argv[1], "-h") == 0) {
    print_usage();
    return 0;
  }

  fprintf(stderr, "[CLI] Unknown command: %s\n\n", argv[1]);
  print_usage();
  return 1;
}
