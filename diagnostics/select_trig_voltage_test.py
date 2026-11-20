Import("env")

# Select only the slow voltage test source for this environment.
env.Replace(PROJECT_SRC_DIR=env.subst("$PROJECT_DIR/diagnostics/trig_voltage_test"))
