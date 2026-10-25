#pragma once

namespace toca::test::fleet {
int replay_skid_dispatch(const char* input_path, const char* output_prefix);
int replay_wheel_queue(const char* input_path, const char* output_prefix);
int replay_whine_rng(const char* input_path, const char* output_prefix);

} // namespace toca::test::fleet
