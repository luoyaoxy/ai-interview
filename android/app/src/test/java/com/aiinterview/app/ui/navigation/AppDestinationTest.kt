package com.aiinterview.app.ui.navigation

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AppDestinationTest {
    @Test
    fun routesAreUniqueAndStable() {
        val routes = AppDestination.entries.map(AppDestination::route)

        assertEquals(3, routes.size)
        assertEquals(routes.size, routes.toSet().size)
        assertTrue(routes.containsAll(listOf("interview", "knowledge", "assistant")))
    }
}
