#include "FicsItNetworksModule.h"

#include "CoreMinimal.h"

#include "AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGGameState.h"
#include "FINGlobalRegisterHelper.h"
#include "Computer/FINComputerRCO.h"
#include "Computer/FINComputerSubsystem.h"
#include "Hologram/FGBuildableHologram.h"
#include "Network/FINNetworkConnectionComponent.h"
#include "Network/FINNetworkAdapter.h"
#include "Network/FINNetworkCable.h"
#include "ModuleSystem/FINModuleSystemPanel.h"
#include "Patching/NativeHookManager.h"
#include "Reflection/FINReflection.h"
#include "UI/FINReflectionStyles.h"
#include "UObject/CoreRedirects.h"

DEFINE_LOG_CATEGORY(LogFicsItNetworks);
IMPLEMENT_GAME_MODULE(FFicsItNetworksModule, FicsItNetworks);

FDateTime FFicsItNetworksModule::GameStart;

#pragma optimize("", off)

void AFGBuildable_Dismantle_Implementation(CallScope<void(*)(IFGDismantleInterface*)>& scope, IFGDismantleInterface* self_r) {
	AFGBuildable* self = dynamic_cast<AFGBuildable*>(self_r);
	TInlineComponentArray<UFINNetworkConnectionComponent*> connectors;
	self->GetComponents(connectors);
	TInlineComponentArray<UFINNetworkAdapterReference*> adapters;
	self->GetComponents(adapters);
	TInlineComponentArray<UFINModuleSystemPanel*> panels;
	self->GetComponents(panels);
	for (UFINNetworkAdapterReference* adapter_ref : adapters) {
		if (AFINNetworkAdapter* adapter = adapter_ref->Ref) {
			connectors.Add(adapter->Connector);
		}
	}
	for (UFINNetworkConnectionComponent* connector : connectors) {
		for (AFINNetworkCable* cable : connector->ConnectedCables) {
			cable->Execute_Dismantle(cable);
		}
	}
	for (UFINNetworkAdapterReference* adapter_ref : adapters) {
		if (AFINNetworkAdapter* adapter = adapter_ref->Ref) {
			adapter->Destroy();
		}
	}
	for (UFINModuleSystemPanel* panel : panels) {
		TArray<AActor*> modules;
		panel->GetModules(modules);
		for (AActor* module : modules) {
			module->Destroy();
		}
	}
}

void AFGBuildable_GetDismantleRefund_Implementation(CallScope<void(*)(const IFGDismantleInterface*, TArray<FInventoryStack>&)>& scope, const IFGDismantleInterface* self_r, TArray<FInventoryStack>& refund) {
	const AFGBuildable* self = dynamic_cast<const AFGBuildable*>(self_r);
	if (!self->IsA<AFINNetworkCable>()) {
		TInlineComponentArray<UFINNetworkConnectionComponent*> components;
		self->GetComponents(components);
		TInlineComponentArray<UFINNetworkAdapterReference*> adapters;
		self->GetComponents(adapters);
		TInlineComponentArray<UFINModuleSystemPanel*> panels;
		self->GetComponents(panels);
		for (UFINNetworkAdapterReference* adapter_ref : adapters) {
			if (AFINNetworkAdapter* adapter = adapter_ref->Ref) {
				components.Add(adapter->Connector);
			}
		}
		for (UFINNetworkConnectionComponent* connector : components) {
			for (AFINNetworkCable* cable : connector->ConnectedCables) {
				cable->Execute_GetDismantleRefund(cable, refund);
			}
		}
		for (UFINModuleSystemPanel* panel : panels) {
			panel->GetDismantleRefund(refund);
		}
	}
}

void FFicsItNetworksModule::StartupModule(){
	GameStart = FDateTime::Now();
	
	TArray<FCoreRedirect> redirects;
	redirects.Add(FCoreRedirect{ECoreRedirectFlags::Type_Class, TEXT("/Script/FicsItNetworks.FINNetworkConnector"), TEXT("/Script/FicsItNetworks.FINAdvancedNetworkConnectionComponent")});
	redirects.Add(FCoreRedirect{ECoreRedirectFlags::Type_Class, TEXT("/Game/FicsItNetworks/Components/Splitter/Splitter.Splitter_C"), TEXT("/FicsItNetworks/Components/CodeableSplitter/CodeableSplitter.CodeableSplitter_C")});
	redirects.Add(FCoreRedirect{ECoreRedirectFlags::Type_Class, TEXT("/Game/FicsItNetworks/Components/Merger/Merger.Merger_C"), TEXT("/FicsItNetworks/Components/CodeableMerger/CodeableMerger.CodeableMerger_C")});

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	TArray<FString> PathsToScan;
	PathsToScan.Add(TEXT("/FicsItNetworks"));
	AssetRegistryModule.Get().ScanPathsSynchronous(PathsToScan, true);
	
	TArray<FAssetData> AssetData;
	AssetRegistryModule.Get().GetAssetsByPath(TEXT("/FicsItNetworks"), AssetData, true);

	for (const FAssetData& Asset : AssetData) {
		FString NewPath = Asset.ObjectPath.ToString();
		FString OldPath = TEXT("/Game") + Asset.ObjectPath.ToString();
		if (Asset.AssetClass == TEXT("Blueprint") || Asset.AssetClass == TEXT("WidgetBlueprint")) {
			NewPath += TEXT("_C");
			OldPath += TEXT("_C");
		}
		// UE_LOG(LogFicsItNetworks, Warning, TEXT("FIN Redirect: '%s' -> '%s'"), *OldPath, *NewPath);
		redirects.Add(FCoreRedirect(ECoreRedirectFlags::Type_AllMask, OldPath, NewPath));
	}

	FCoreRedirects::AddRedirectList(redirects, "FIN-Code");
	
	FCoreDelegates::OnPostEngineInit.AddStatic([]() {
#if !WITH_EDITOR
		SUBSCRIBE_METHOD_VIRTUAL(AFGBuildableHologram::SetupComponent, (void*)GetDefault<AFGBuildableHologram>(), [](auto& scope, AFGBuildableHologram* self, USceneComponent* attachParent, UActorComponent* componentTemplate, const FName& componentName) {
			UStaticMesh* networkConnectorHoloMesh = LoadObject<UStaticMesh>(NULL, TEXT("/FicsItNetworks/Network/Mesh_NetworkConnector.Mesh_NetworkConnector"), NULL, LOAD_None, NULL);
			if (componentTemplate->IsA<UFINNetworkConnectionComponent>()) {
				auto comp = NewObject<UStaticMeshComponent>(attachParent);
				comp->RegisterComponent();
				comp->SetMobility(EComponentMobility::Movable);
				comp->SetStaticMesh(networkConnectorHoloMesh);
				comp->AttachToComponent(attachParent, FAttachmentTransformRules::KeepRelativeTransform);
				comp->SetRelativeTransform(Cast<USceneComponent>(componentTemplate)->GetRelativeTransform());
				comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					
				scope.Override(comp);
			}
		});

		SUBSCRIBE_METHOD_VIRTUAL(IFGDismantleInterface::Dismantle_Implementation, (void*)static_cast<const IFGDismantleInterface*>(GetDefault<AFGBuildable>()), &AFGBuildable_Dismantle_Implementation)
		SUBSCRIBE_METHOD_VIRTUAL(IFGDismantleInterface::GetDismantleRefund_Implementation, (void*)static_cast<const IFGDismantleInterface*>(GetDefault<AFGBuildable>()), &AFGBuildable_GetDismantleRefund_Implementation)
		
		SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCharacterPlayer::BeginPlay, (void*)GetDefault<AFGCharacterPlayer>(), [](AActor* self) {
			AFGCharacterPlayer* character = Cast<AFGCharacterPlayer>(self);
			if (character) {
				AFINComputerSubsystem* SubSys = AFINComputerSubsystem::GetComputerSubsystem(self->GetWorld());
				if (SubSys) SubSys->AttachWidgetInteractionToPlayer(character);
			}
		})

		SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCharacterPlayer::EndPlay, (void*)GetDefault<AFGCharacterPlayer>(), [](AActor* self, EEndPlayReason::Type Reason) {
			AFGCharacterPlayer* character = Cast<AFGCharacterPlayer>(self);
			if (character) {
				AFINComputerSubsystem* SubSys = AFINComputerSubsystem::GetComputerSubsystem(self->GetWorld());
				if (SubSys) SubSys->DetachWidgetInteractionToPlayer(character);
			}
		})

		SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGGameMode::PostLogin, (void*)GetDefault<AFGGameMode>(), [](AFGGameMode* gm, APlayerController* pc) {
			if (gm->HasAuthority() && !gm->IsMainMenuGameMode()) {
				gm->RegisterRemoteCallObjectClass(UFINComputerRCO::StaticClass());

				UClass* ModuleRCO = LoadObject<UClass>(NULL, TEXT("/FicsItNetworks/Components/ModularPanel/Modules/Module_RCO.Module_RCO_C"));
				check(ModuleRCO);
				gm->RegisterRemoteCallObjectClass(ModuleRCO);
			}
		});
#else
		/*FFINGlobalRegisterHelper::Register();
			
		FFINReflection::Get()->PopulateSources();
		FFINReflection::Get()->LoadAllTypes();*/
#endif
	});
}
#pragma optimize("", on)

void FFicsItNetworksModule::ShutdownModule() {
	FFINReflectionStyles::Shutdown();
}

extern "C" DLLEXPORT void BootstrapModule(std::ofstream& logFile) {
	
}
