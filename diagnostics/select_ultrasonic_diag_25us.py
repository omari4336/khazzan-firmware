Import("env")

# Keep the experimental source separate from all existing environments.
env.Replace(PROJECT_SRC_DIR=env.subst("$PROJECT_DIR/diagnostics/ultrasonic_diag_25us"))
