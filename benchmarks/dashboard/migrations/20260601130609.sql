-- Add column "is_raw" to table: "BenchRun"
ALTER TABLE `BenchRun` ADD COLUMN `is_raw` integer NOT NULL DEFAULT 0;
