-- Disable the enforcement of foreign-keys constraints
PRAGMA foreign_keys = off;
-- Create "new_BenchRun" table
CREATE TABLE `new_BenchRun` (`id` integer NULL PRIMARY KEY AUTOINCREMENT, `git_hash` text NOT NULL, `branch` text NOT NULL, `timestamp` text NOT NULL, `hostname` text NOT NULL, `compiler` text NOT NULL, `filter` text NOT NULL, `is_full_run` integer NOT NULL DEFAULT 0, `is_raw` integer NOT NULL DEFAULT 0);
-- Copy rows from old table "BenchRun" to new temporary table "new_BenchRun"
INSERT INTO `new_BenchRun` (`id`, `git_hash`, `branch`, `timestamp`, `hostname`, `compiler`, `filter`, `is_full_run`, `is_raw`) SELECT `id`, `git_hash`, `branch`, `timestamp`, `hostname`, `compiler`, `filter`, IFNULL(`is_full_run`, 0) AS `is_full_run`, `is_raw` FROM `BenchRun`;
-- Drop "BenchRun" table after copying rows
DROP TABLE `BenchRun`;
-- Rename temporary table "new_BenchRun" to "BenchRun"
ALTER TABLE `new_BenchRun` RENAME TO `BenchRun`;
-- Enable back the enforcement of foreign-keys constraints
PRAGMA foreign_keys = on;
