Import("env")

# Compile only the independent RTC test source.
env.Replace(PROJECT_SRC_DIR=env.subst("$PROJECT_DIR/diagnostics/rtc_test"))
