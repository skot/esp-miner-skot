import { ComponentFixture, fakeAsync, TestBed, tick } from '@angular/core/testing';
import { of, throwError } from 'rxjs';
import { AsicDiagnosticsComponent } from './asic-diagnostics.component';
import { AsicDiagnostics } from '../../generated/models';
import { SystemApiService } from '../../services/system.service';

function sample(): AsicDiagnostics {
  return {
    supported: 1, mining: 1, source: 'mining', startupResult: 'passed',
    status: 'Mining', epoch: 2, leadingZeros: 24,
    chips: [{ chipId: 0, valid: 1, frequency: 600, windowSeconds: 4, ageSeconds: 1,
      corePassCounts: Array(156).fill(1000), coreFailCounts: Array(156).fill(2) }]
  };
}

describe('AsicDiagnosticsComponent', () => {
  let component: AsicDiagnosticsComponent;
  let fixture: ComponentFixture<AsicDiagnosticsComponent>;
  let api: jasmine.SpyObj<SystemApiService>;
  beforeEach(() => {
    api = jasmine.createSpyObj('SystemApiService', ['getDiagnostics']);
    api.getDiagnostics.and.returnValue(of(sample()));
    TestBed.configureTestingModule({ imports: [AsicDiagnosticsComponent], providers: [
      { provide: SystemApiService, useValue: api }
    ] });
    fixture = TestBed.createComponent(AsicDiagnosticsComponent);
    component = fixture.componentInstance;
  });

  it('maps all 156 cores to four domains and converts PASS to GH/s', () => {
    component.setSnapshot(sample());
    const chip = component.chips[0];
    expect(chip.domains.map(d => d.cores.length)).toEqual([39, 39, 39, 39]);
    expect(chip.domains[2].cores[0].id).toBe(78);
    expect(chip.domains[3].cores[38].id).toBe(155);
    expect(chip.domains[0].pass).toBe(39000);
    expect(chip.domains[0].fail).toBe(78);
    expect(chip.domains[0].ghs).toBeCloseTo(163.577856, 6);
    expect(chip.ghs).toBeCloseTo(654.311424, 6);
  });

  it('shows a failed zero-PASS domain after shutdown without labelling it live', () => {
    const data = sample();
    data.mining = 0; data.source = 'startup'; data.startupResult = 'failed';
    data.status = 'Warm ASIC startup failed';
    data.chips[0].corePassCounts.fill(0, 78, 117);
    component.setSnapshot(data);
    fixture.detectChanges();
    const domain = component.chips[0].domains[2];
    expect(domain.pass).toBe(0); expect(domain.zero).toBe(39); expect(domain.ghs).toBe(0);
    expect(fixture.nativeElement.textContent).toContain('Retained measurement');
    expect(fixture.nativeElement.textContent).toContain('39/39');
    expect(fixture.nativeElement.querySelectorAll('tbody tr.silent').length).toBe(1);
  });

  it('distinguishes an unavailable reading from zero PASS', () => {
    const data = sample();
    data.chips[0].valid = 0; data.chips[0].corePassCounts = []; data.chips[0].coreFailCounts = [];
    component.setSnapshot(data); fixture.detectChanges();
    expect(component.chips[0].domains.length).toBe(0);
    expect(fixture.nativeElement.textContent).toContain('Counters unavailable');
    expect(fixture.nativeElement.querySelectorAll('tbody tr').length).toBe(0);
  });

  it('keeps one isolated silent core separate from a silent domain', () => {
    const data = sample(); data.chips[0].corePassCounts[78] = 0;
    component.setSnapshot(data);
    expect(component.chips[0].domains[2].zero).toBe(1);
    expect(component.chips[0].domains[2].pass).toBe(38000);
  });

  it('marks old measurements retained even if mining is enabled', () => {
    const data = sample(); data.chips[0].ageSeconds = 20;
    component.setSnapshot(data); expect(component.retained).toBeTrue();
  });

  it('keeps sensors aligned by chip ID and preserves millivolt precision', () => {
    component.info = {
      asicTemps: [53.5, 52.2], asicVoltages: [1.157, 1.163], domainVoltages: [1157, 1163],
      regulatorVoltage: 2.320, actualFrequency: 600, power: 46.7,
      hashrateMonitor: { asics: [] }
    };
    const data = sample(); data.chips[0].chipId = 1;
    component.setSnapshot(data); fixture.detectChanges();
    const row = fixture.nativeElement.querySelector('.asic-row');
    expect(row.textContent).toContain('A2');
    expect(row.textContent).toContain('52.2 °C');
    expect(row.textContent).toContain('1.163 V');
    expect(row.textContent).not.toContain('53.5 °C');
    expect(fixture.nativeElement.querySelector('.supply').textContent).toContain('2.320 V');
    expect(component.stackDifferenceMv).toBe(6);
  });

  it('does not display a partial counter sum as total system hashrate', () => {
    const data = sample();
    data.chips.push({ ...data.chips[0], chipId: 1, valid: 0, corePassCounts: [], coreFailCounts: [] });
    component.setSnapshot(data);
    expect(component.totalGhs).toBeNull();
    expect(component.measuredDomains).toBe(4);
    expect(component.activeDomains).toBe(4);
  });

  it('shows absent or invalid sensors as unavailable', () => {
    component.info = {
      asicTemps: [-1], asicVoltages: [0], domainVoltages: [1160, 0],
      regulatorVoltage: 0, actualFrequency: 600, power: 0,
      hashrateMonitor: { asics: [] }
    };
    component.setSnapshot(sample()); fixture.detectChanges();
    expect(component.stackDifferenceMv).toBeNull();
    expect(fixture.nativeElement.querySelector('.sensors').textContent).not.toContain('0.000 V');
    expect(fixture.nativeElement.querySelector('.sensors').textContent).not.toContain('-1.0 °C');
    expect(component.positive(NaN)).toBeFalse();
  });

  it('labels stale telemetry separately from fresh counters', () => {
    component.telemetryStale = true;
    component.setSnapshot(sample()); fixture.detectChanges();
    expect(fixture.nativeElement.textContent).toContain('Voltages and temperatures are last received values');
    expect(component.retained).toBeFalse();
  });

  it('continues polling after an error and stops when destroyed', fakeAsync(() => {
    fixture.detectChanges(); tick(0);
    expect(api.getDiagnostics).toHaveBeenCalledTimes(1);
    api.getDiagnostics.and.returnValue(throwError(() => new Error('offline')));
    tick(5000); expect(component.error).toBeTrue(); expect(component.chips.length).toBe(1);
    api.getDiagnostics.and.returnValue(of(sample()));
    tick(5000); expect(component.error).toBeFalse();
    fixture.destroy(); tick(10000);
    expect(api.getDiagnostics).toHaveBeenCalledTimes(3);
  }));
});
