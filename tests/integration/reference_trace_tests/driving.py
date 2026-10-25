"""Source trace checks for driving."""

from .support import TRACE, TraceTestCase, vehicle


class DrivingPolicyTests(TraceTestCase):
    def test_fleet_pursuit_selects_one_real_forward_actor_and_uses_physical_steering(self):
        initial = vehicle()
        initial.update(actor=2, actors=[{**vehicle(position=(10 * 65536, 0, -2 * 65536)), "actor": 0},
                                        {**vehicle(position=(-65536, 0, 0)), "actor": 1},
                                        {**vehicle(), "actor": 2}])
        policy = TRACE.FleetPursuitPolicy(initial)
        keys, decision = policy.choose(0, initial)
        self.assertEqual(policy.target, 0)
        self.assertEqual(keys, {"Up", "Right"})
        self.assertEqual(decision["target_actor"], 0)
        self.assertLess(decision["signed_angle_radians"], 0)
        self.assertEqual(policy.choose(3, initial)[0], {"Up"})
        initial["actors"][0] = {**vehicle(position=(10 * 65536, 0, 2 * 65536)), "actor": 0}
        self.assertEqual(policy.choose(4, initial)[0], {"Up", "Left"})
        initial["actors"][0] = {**vehicle(position=(-3 * 65536, 0, 0)), "actor": 0}
        self.assertEqual(policy.choose(5, initial)[0], {"Down"})
        self.assertEqual(policy.target, 0)

    def test_brake_lamps_scenario_releases_real_brake_and_then_coasts(self):
        policy = TRACE.InputPolicy("brake-lamps", 64, vehicle(speed=65536))
        actual = [policy.choose(index, vehicle(speed=65536))[0] for index in range(64)]
        self.assertEqual(actual, [{"Up"}] * 16 + [{"Down"}] * 16 + [{"Up"}] * 16 + [set()] * 16)
        self.assertTrue(policy.parameters["keys_only"])

    def test_shuttle_brakes_real_initial_speed_then_reverses_at_observed_bounds(self):
        fast = vehicle(speed=65536)
        policy = TRACE.InputPolicy("shuttle", 500, fast)
        self.assertEqual(policy.choose(0, fast)[0], {"Down"})
        stopped = vehicle(position=(100 * 65536, 0, 0), speed=1000)
        self.assertEqual(policy.choose(50, stopped)[0], {"Up"})
        far = vehicle(position=(120 * 65536, 0, 0), velocity=(10000, 0, 0), speed=10000)
        keys, decision = policy.choose(51, far)
        self.assertEqual(keys, {"Down"})
        self.assertEqual(decision["longitudinal_q16"], 20 * 65536)
        back = vehicle(position=(80 * 65536, 0, 0), velocity=(-10000, 0, 0), speed=10000)
        self.assertEqual(policy.choose(52, back)[0], {"Up"})
        with self.assertRaisesRegex(RuntimeError, "bounds"):
            policy.choose(53, vehicle(position=(500 * 65536, 0, 0)))

    def test_slalom_uses_logged_modest_target_and_interleaves_neutral_steering(self):
        initial = vehicle(speed=1000)
        policy = TRACE.InputPolicy("slalom-shuttle", 500, initial)
        keys, decision = policy.choose(0, initial)
        self.assertEqual(keys, {"Up", "Left"})
        self.assertEqual(decision["target_lateral_q16"], 3 * 65536 // 2)
        self.assertEqual(policy.choose(1, initial)[0], {"Up"})
        self.assertEqual(policy.choose(60, initial)[0], {"Up", "Right"})

    def test_shuttle_releases_pedal_at_observed_speed_but_brakes_opposite_motion(self):
        initial = vehicle(speed=1000)
        policy = TRACE.InputPolicy("shuttle", 500, initial)
        policy.choose(0, initial)
        fast_forward = vehicle(velocity=(40000, 0, 0), speed=40000)
        keys, decision = policy.choose(1, fast_forward)
        self.assertEqual(keys, set())
        self.assertTrue(decision["coasting"])
        fast_reverse = vehicle(velocity=(-40000, 0, 0), speed=40000)
        keys, decision = policy.choose(2, fast_reverse)
        self.assertEqual(keys, {"Up"})
        self.assertFalse(decision["coasting"])

    def test_coverage_uses_consumed_inputs_and_real_motion_not_requested_activity(self):
        initial = vehicle()
        coverage = TRACE.Coverage(initial)
        for _ in range(500):
            coverage.consume(initial, initial)
        summary = coverage.summary("offroad-shuttle")
        self.assertFalse(summary["passed"])
        self.assertEqual(summary["path_distance_metres"], 0)
        self.assertEqual(summary["steered_moving_updates"], 0)
        for index in range(500):
            active = vehicle(position=(index * 65536, 0, 0), velocity=(-10000, 0, 0), speed=10000, surface=2)
            active["input_q16"].update(left=65536, brake=65536)
            coverage.consume(active, active)
        summary = coverage.summary("offroad-shuttle")
        # Five hundred active samples do not conceal the preceding padding:
        # this scenario requires a moving majority over the complete record.
        self.assertFalse(summary["passed"])
        self.assertEqual(summary["offroad_moving_updates"], 500)
        self.assertEqual(summary["surface_wheel_samples"]["grass"], 2000)

    def test_offroad_drives_to_real_grounded_grass_then_brakes_before_anchor(self):
        initial = vehicle(speed=1000)
        policy = TRACE.InputPolicy("offroad-shuttle", 500, initial)
        keys, decision = policy.choose(0, initial)
        self.assertEqual(keys, {"Up", "Right"})
        self.assertEqual(decision["phase"], "grass-entry")
        grass = vehicle(position=(20 * 65536, 0, -11 * 65536), velocity=(10000, 0, 0), speed=10000, surface=2)
        keys, decision = policy.choose(40, grass)
        self.assertEqual(keys, {"Down"})
        self.assertEqual(decision["phase"], "grass-brake")
        stopped = vehicle(position=(23 * 65536, 0, -12 * 65536), speed=1000, surface=2)
        keys, decision = policy.choose(55, stopped)
        self.assertEqual(keys, {"Up"})
        self.assertEqual(decision["phase"], "grass-forward")
        self.assertEqual(decision["anchor_position_q16"], (23 * 65536, 0, -12 * 65536))
        self.assertEqual(decision["grass_entry_update"], 40)

    def test_airborne_grass_samples_do_not_trigger_entry_or_offroad_coverage(self):
        initial = vehicle(speed=1000)
        policy = TRACE.InputPolicy("offroad-shuttle", 500, initial)
        policy.choose(0, initial)
        grass = vehicle(position=(20 * 65536, 0, -11 * 65536), speed=10000, surface=2)
        body = bytearray.fromhex(grass["body_hex"])
        body[0x517:0x51b] = bytes([1] * 4)
        grass["body_hex"] = body.hex()
        self.assertEqual(policy.choose(40, grass)[1]["phase"], "grass-entry")
        coverage = TRACE.Coverage(initial)
        coverage.consume(grass, grass)
        self.assertEqual(coverage.summary("offroad-shuttle")["offroad_moving_updates"], 0)
