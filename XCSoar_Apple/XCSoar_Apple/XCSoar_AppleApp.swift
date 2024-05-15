//
//  XCSoar_AppleApp.swift
//  XCSoar_Apple
//
//  Created by Michel Storms on 15/05/2024.
//

import SwiftUI

@main
struct XCSoar_AppleApp: App {
    let persistenceController = PersistenceController.shared

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environment(\.managedObjectContext, persistenceController.container.viewContext)
        }
    }
}
