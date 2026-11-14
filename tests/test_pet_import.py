#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Artwork-tool regressions (requires the importer's Pillow and NumPy).

Run with: python3 -I tests/test_pet_import.py
"""
import importlib.util
from pathlib import Path
import unittest

import numpy as np

spec = importlib.util.spec_from_file_location(
    'pet_import', Path(__file__).resolve().parents[1] / 'tools/import-pet-frames.py')
pet_import = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pet_import)


class PlacementTests(unittest.TestCase):
    @staticmethod
    def frames(tail=False):
        frame = np.zeros((128, 128, 4), dtype=np.uint8)
        frame[20:100, 88:101] = [220, 120, 60, 255]
        if tail:
            frame[85:98, 8:90] = [245, 210, 160, 255]
        return [frame.copy() for _ in range(24)]

    def test_long_tail_is_preserved_in_every_pose(self):
        original = self.frames(tail=True)
        placed, _ = pet_import.place(original)
        for i, (before, after) in enumerate(zip(original, placed)):
            with self.subTest(cell=i):
                self.assertEqual(np.count_nonzero(before[..., 3]), np.count_nonzero(after[..., 3]))
                left, _, right, _ = pet_import.bbox(after)
                self.assertGreaterEqual(left, 1)
                self.assertLessEqual(right, 126)

    def test_one_sheet_offset_preserves_the_walk(self):
        original = self.frames(tail=True)
        # Leg changes below the upper body must not change its anchor.
        for i in range(6, 12):
            original[i][98:103, 88 + (i % 3):94 + (i % 3)] = [220, 120, 60, 255]
        placed, _ = pet_import.place(original)
        centres = [pet_import.body_centre(frame) for frame in placed[6:12]]
        self.assertEqual(len(set(centres)), 1)

    def test_grounded_and_carried_frames_stand_on_the_ground(self):
        placed, _ = pet_import.place(self.frames())
        for i, frame in enumerate(placed):
            if i not in pet_import.ACTS:
                with self.subTest(cell=i):
                    self.assertEqual(pet_import.bbox(frame)[3], pet_import.GROUND)

    def test_a_tall_frame_can_use_the_atlas_top_row_without_losing_pixels(self):
        original = self.frames()
        original[0].fill(0)
        original[0][:117, 60:80] = [220, 120, 60, 255]
        placed, _ = pet_import.place(original)
        self.assertEqual(pet_import.bbox(placed[0])[1], 0)
        self.assertEqual(np.count_nonzero(placed[0][..., 3]), 117 * 20)

    def test_a_frame_that_cannot_fit_is_rejected_instead_of_cropped(self):
        original = self.frames()
        original[0].fill(0)
        original[0][:118, 60:80] = [220, 120, 60, 255]
        with self.assertRaisesRegex(ValueError, 'cell 1: placing would clip'):
            pet_import.place(original)

    def test_an_action_can_float_above_the_ground_but_never_below_it(self):
        original = self.frames()
        original[21][90:128, 90:94] = [220, 120, 60, 255]
        placed, _ = pet_import.place(original)
        self.assertEqual(pet_import.bbox(placed[21])[3], pet_import.GROUND)
        self.assertEqual(np.count_nonzero(original[21][..., 3]), np.count_nonzero(placed[21][..., 3]))


if __name__ == '__main__':
    unittest.main()
