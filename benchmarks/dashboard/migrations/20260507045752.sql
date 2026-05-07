-- Add column "row_kind" to table: "BenchResult"
ALTER TABLE `BenchResult` ADD COLUMN `row_kind` text NOT NULL;
-- Add column "complexity_class" to table: "BenchResult"
ALTER TABLE `BenchResult` ADD COLUMN `complexity_class` text NOT NULL;
-- Add column "complexity_coef" to table: "BenchResult"
ALTER TABLE `BenchResult` ADD COLUMN `complexity_coef` real NOT NULL;
-- Add column "rms_pct" to table: "BenchResult"
ALTER TABLE `BenchResult` ADD COLUMN `rms_pct` real NOT NULL;
