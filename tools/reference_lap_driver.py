"""Read-only route feedback choosing real keys for bounded original recordings.

This is acquisition input, not recovered game AI. It uses the conservative
policy from the native full-lap test, with original live POINTS and body state.
No game data, clocks, physics parameters or controller ownership are changed.
"""

import math
import struct
import zlib


ONE = 65536


class SourceLapDriver:
    def __init__(self, initialized_points, initial_snapshot):
        if len(initialized_points) < 4:
            raise ValueError("Missing initialized POINTS count")
        count, = struct.unpack_from("<I", initialized_points)
        if not 6 <= count <= 65535 or len(initialized_points) != 4 + count * 64:
            raise ValueError("Invalid initialized POINTS records")
        self.points = [tuple(value / ONE for value in struct.unpack_from("<3i", initialized_points, 4 + index * 64 + 12)[::2])
                       for index in range(count)]
        self.actor = initial_snapshot["actor"]
        self.previous_forward = None
        self.parameters = {"version": 1, "keys_only": True, "points_crc32": f"{zlib.crc32(initialized_points):08x}",
                           "point_count": count, "lookahead": 12, "yaw_damping": 8, "steering_deadband": .025,
                           "minimum_speed": .42, "maximum_speed": .8, "corner_slowdown": .8, "brake_margin": .1,
                           "policy": "native full-lap diagnostic feedback; not original AI"}

    def target_ahead(self, next_point, position):
        start, end = self.points[(next_point - 1) % len(self.points)], self.points[next_point]
        dx, dz = end[0] - start[0], end[1] - start[1]
        length = math.hypot(dx, dz)
        projection = max(0, min(length, ((position[0] - start[0]) * dx + (position[1] - start[1]) * dz) / length)) if length else 0
        distance = projection + self.parameters["lookahead"]
        for offset in range(len(self.points)):
            if length and distance <= length:
                return start[0] + dx * distance / length, start[1] + dz * distance / length
            distance -= length
            start, end = end, self.points[(next_point + offset + 1) % len(self.points)]
            dx, dz = end[0] - start[0], end[1] - start[1]
            length = math.hypot(dx, dz)
        raise ValueError("Driver lookahead exhausted initialized route")

    def choose(self, snapshot):
        if snapshot["actor"] != self.actor:
            raise ValueError("Local driver identity changed during acquisition")
        body, dynamics, game = (bytes.fromhex(snapshot[name + "_hex"]) for name in ("body", "dynamics", "gameplay"))
        if (len(body), len(dynamics), len(game)) != (0x53c, 0x338, 0x124):
            raise ValueError("Local driver requires complete original records")
        human, = struct.unpack_from("<I", game, 8)
        finished, = struct.unpack_from("<I", game, 0x54)
        point, = struct.unpack_from("<h", game, 0xa2)
        lap, = struct.unpack_from("<h", game, 0xa8)
        state = snapshot["session_state"]
        info = {"actor": self.actor, "route_next": point, "lap": lap, "checkpoint": game[0xa6],
                "checkpoint_marker": game[0x9d], "human": human, "finished": finished, "session_state": state}
        if human == 0 or finished or state in (7, 8, 10, 13):
            self.previous_forward = None
            return set(), {**info, "action": "coast-owned-controller"}
        transmission, = struct.unpack_from("<I", dynamics, 0x27c)
        if transmission != 2:
            raise ValueError("Source lap keyboard driver requires the original automatic transmission")
        if not 0 <= point < len(self.points):
            raise ValueError("Local driver route cursor is outside initialized POINTS")
        position = tuple(value / ONE for value in struct.unpack_from("<3i", body, 0xbc)[::2])
        forward = tuple(value / ONE for value in struct.unpack_from("<3i", body, 0x150)[::2])
        speed = struct.unpack_from("<i", body, 0xe0)[0] / ONE
        target = self.target_ahead(point, position)
        dx, dz = target[0] - position[0], target[1] - position[1]
        error = math.atan2(forward[0] * dz - forward[1] * dx, forward[0] * dx + forward[1] * dz)
        previous = self.previous_forward
        yaw = 0 if previous is None else math.atan2(previous[0] * forward[1] - previous[1] * forward[0],
                                                   previous[0] * forward[0] + previous[1] * forward[1])
        steering = error - self.parameters["yaw_damping"] * yaw
        desired_speed = max(self.parameters["minimum_speed"], self.parameters["maximum_speed"] - abs(error) * self.parameters["corner_slowdown"])
        keys = {"Up"} if speed < desired_speed else {"Down"} if speed > desired_speed + self.parameters["brake_margin"] else set()
        if abs(steering) > self.parameters["steering_deadband"]:
            keys.add("Left" if steering > 0 else "Right")
        self.previous_forward = forward
        return keys, {**info, "action": "drive", "position": position, "forward": forward, "target": target,
                      "speed": speed, "desired_speed": desired_speed, "error_radians": error, "yaw_radians": yaw,
                      "steering_radians": steering}
