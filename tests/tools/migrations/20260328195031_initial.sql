-- Create "Person" table
CREATE TABLE `Person` (
  `id` integer NULL PRIMARY KEY AUTOINCREMENT,
  `name` text NOT NULL,
  `age` integer NOT NULL,
  `salary` real NOT NULL,
  `is_active` integer NOT NULL,
  `years_experience` integer NOT NULL,
  `department` text NOT NULL,
  `score` integer NULL,
  `nickname` text NULL,
  `avatar` blob NULL
);
-- Create index "Person_name" to table: "Person"
CREATE UNIQUE INDEX `Person_name` ON `Person` (`name`);
-- Create index "idx_Person_name" to table: "Person"
CREATE UNIQUE INDEX `idx_Person_name` ON `Person` (`name`);
-- Create index "idx_Person_department" to table: "Person"
CREATE INDEX `idx_Person_department` ON `Person` (`department`);
-- Create index "idx_Person_department_age" to table: "Person"
CREATE INDEX `idx_Person_department_age` ON `Person` (`department`, `age`);
-- Create index "idx_Person_name_department" to table: "Person"
CREATE UNIQUE INDEX `idx_Person_name_department` ON `Person` (`name`, `department`);
-- Create "SimpleRecord" table
CREATE TABLE `SimpleRecord` (
  `id` integer NULL PRIMARY KEY AUTOINCREMENT,
  `name` text NOT NULL,
  `value` integer NOT NULL
);
-- Create "Message" table
CREATE TABLE `Message` (
  `id` integer NULL PRIMARY KEY AUTOINCREMENT,
  `content` text NOT NULL,
  `value` integer NOT NULL,
  `sender_id` integer NOT NULL
);
-- Create index "idx_Message_sender_id" to table: "Message"
CREATE INDEX `idx_Message_sender_id` ON `Message` (`sender_id`);
-- Create "ExtendedTypes" table
CREATE TABLE `ExtendedTypes` (
  `id` integer NULL PRIMARY KEY AUTOINCREMENT,
  `big_num` integer NOT NULL,
  `precise` real NOT NULL,
  `approx` real NOT NULL,
  `u_int` integer NOT NULL,
  `ll_signed` integer NOT NULL,
  `opt_double` real NULL,
  `opt_int64` integer NULL,
  `label` text NOT NULL,
  `tiny_signed` integer NOT NULL,
  `tiny_unsigned` integer NOT NULL,
  `single_char` integer NOT NULL,
  `color` integer NOT NULL,
  `date_field` text NOT NULL,
  `datetime_field` text NOT NULL,
  `duration_field` integer NOT NULL,
  `file_path` text NOT NULL,
  `raw_data` blob NULL,
  `uuid_field` text NOT NULL,
  `opt_color` integer NULL,
  `opt_timestamp` text NULL,
  `opt_path` text NULL
);
-- Create "Task" table
CREATE TABLE `Task` (
  `id` integer NULL PRIMARY KEY AUTOINCREMENT,
  `assignee_id` integer NOT NULL,
  `reviewer_id` integer NOT NULL,
  `description` text NOT NULL
);
-- Create index "idx_Task_assignee_id" to table: "Task"
CREATE INDEX `idx_Task_assignee_id` ON `Task` (`assignee_id`);
-- Create index "idx_Task_reviewer_id" to table: "Task"
CREATE INDEX `idx_Task_reviewer_id` ON `Task` (`reviewer_id`);
