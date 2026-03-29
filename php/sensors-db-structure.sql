SET SQL_MODE = "NO_AUTO_VALUE_ON_ZERO";
START TRANSACTION;
SET time_zone = "+00:00";


/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8mb4 */;

--
-- Database: `sensors`
--

-- --------------------------------------------------------

--
-- Table structure for table `SENSOR`
--

CREATE TABLE `SENSOR` (
  `ID_SENSOR` int(11) NOT NULL,
  `SENSOR_NAME` varchar(255) NOT NULL,
  `API_KEY` varchar(255) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `SENSOR_DATA`
--

CREATE TABLE `SENSOR_DATA` (
  `ID_SENSOR` int(11) NOT NULL,
  `DT` datetime NOT NULL COMMENT 'Čas v zóně serveru',
  `VAL` bigint(20) NOT NULL,
  `ADDITIONAL` varchar(255) DEFAULT NULL,
  `DT_GMT` datetime DEFAULT NULL COMMENT 'Volitelný čas v zóně GMT - posílá sensor'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

--
-- Indexes for dumped tables
--

--
-- Indexes for table `SENSOR`
--
ALTER TABLE `SENSOR`
  ADD PRIMARY KEY (`ID_SENSOR`);

--
-- Indexes for table `SENSOR_DATA`
--
ALTER TABLE `SENSOR_DATA`
  ADD PRIMARY KEY (`ID_SENSOR`,`DT`);

--
-- AUTO_INCREMENT for dumped tables
--

--
-- AUTO_INCREMENT for table `SENSOR`
--
ALTER TABLE `SENSOR`
  MODIFY `ID_SENSOR` int(11) NOT NULL AUTO_INCREMENT;

--
-- Constraints for dumped tables
--

--
-- Constraints for table `SENSOR_DATA`
--
ALTER TABLE `SENSOR_DATA`
  ADD CONSTRAINT `FK_SENSOR_DATA` FOREIGN KEY (`ID_SENSOR`) REFERENCES `SENSOR` (`ID_SENSOR`);
COMMIT;

/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
