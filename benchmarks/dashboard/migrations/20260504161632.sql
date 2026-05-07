-- Create "BenchRun" table
CREATE TABLE `BenchRun` (`id` integer NULL PRIMARY KEY AUTOINCREMENT, `git_hash` text NOT NULL, `branch` text NOT NULL, `timestamp` text NOT NULL, `hostname` text NOT NULL, `compiler` text NOT NULL, `filter` text NOT NULL, `is_full_run` integer NOT NULL);
-- Create "BenchResult" table
CREATE TABLE `BenchResult` (`id` integer NULL PRIMARY KEY AUTOINCREMENT, `run_id` integer NOT NULL, `test_name` text NOT NULL, `category` text NOT NULL, `dataset_size` integer NOT NULL, `real_time_ns` real NOT NULL, `cpu_time_ns` real NOT NULL, `iterations` integer NOT NULL, `items_per_second` real NOT NULL);
-- Create index "idx_BenchResult_run_id" to table: "BenchResult"
CREATE INDEX `idx_BenchResult_run_id` ON `BenchResult` (`run_id`);
