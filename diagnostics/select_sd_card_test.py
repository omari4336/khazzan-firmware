Import("env")

# Isolate SD testing from all other firmware sources.
env.Replace(PROJECT_SRC_DIR=env.subst("$PROJECT_DIR/diagnostics/sd_card_test"))
