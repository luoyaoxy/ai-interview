package com.aiinterview.app.ui

import android.net.Uri
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.aiinterview.app.ui.assistant.AssistantScreen
import com.aiinterview.app.ui.interview.InterviewScreen
import com.aiinterview.app.ui.knowledge.KnowledgeScreen
import com.aiinterview.app.ui.navigation.AppDestination

@Composable
fun AiInterviewApp(
    sharedResumeUri: Uri? = null,
    onSharedResumeConsumed: () -> Unit = {},
) {
    val navController = rememberNavController()
    val backStackEntry by navController.currentBackStackEntryAsState()
    val currentRoute = backStackEntry?.destination?.route

    Scaffold(
        bottomBar = {
            NavigationBar {
                AppDestination.entries.forEach { destination ->
                    NavigationBarItem(
                        selected = currentRoute == destination.route,
                        onClick = {
                            navController.navigate(destination.route) {
                                popUpTo(navController.graph.findStartDestination().id) {
                                    saveState = true
                                }
                                launchSingleTop = true
                                restoreState = true
                            }
                        },
                        icon = { Text(destination.shortLabel) },
                        label = { Text(destination.label) },
                    )
                }
            }
        },
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = AppDestination.Interview.route,
            modifier = Modifier.padding(innerPadding),
        ) {
            composable(AppDestination.Interview.route) {
                InterviewScreen(
                    sharedResumeUri = sharedResumeUri,
                    onSharedResumeConsumed = onSharedResumeConsumed,
                )
            }
            composable(AppDestination.Knowledge.route) { KnowledgeScreen() }
            composable(AppDestination.Assistant.route) { AssistantScreen() }
        }
    }
}
