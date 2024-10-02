import unittest

import pyrel2sql


class EqualityTest(unittest.TestCase):

    def test_equality(self):
        self.assertEqual(
            pyrel2sql.translate("def F {(1, 2); (3, 4)}"),
            "CREATE VIEW F AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2));",
        )


if __name__ == "__main__":
    unittest.main()
