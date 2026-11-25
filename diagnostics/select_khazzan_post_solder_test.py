Import("env")

# Isolated source; no previous diagnostic or production source is compiled.
env.Replace(PROJECT_SRC_DIR=env.subst("$PROJECT_DIR/diagnostics/post_solder_test"))
