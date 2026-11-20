Import("env")

# Select a separate source tree for this environment only.
env.Replace(PROJECT_SRC_DIR=env.subst("$PROJECT_DIR/diagnostics/ultrasonic_diag"))
