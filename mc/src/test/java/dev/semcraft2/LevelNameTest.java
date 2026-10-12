package dev.semcraft2;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;

class LevelNameTest {
	@Test
	void commandLineAndMenuNamesAreOneLevel() {
		assertEquals("levels\\11_alleyofsphinxes.wld", LevelShell.normalize("levels\\..\\levels\\11_alleyofsphinxes.wld"));
		assertEquals("levels\\levelsmp\\1_1_palenque.wld", LevelShell.normalize("levels\\..\\levels\\levelsmp\\1_1_palenque.wld"));
		assertEquals("levels\\01_hatshepsut.wld", LevelShell.normalize("levels\\01_hatshepsut.wld"));
		assertEquals("..\\x.wld", LevelShell.normalize("..\\x.wld"));
	}
}
